#pragma once

// json_protocol.h
// JSON-based message protocol for WebSocket communication

#include "core/types.h"
#include "core/result.h"
#include "auth/account.h"
#include "item/item_attribute.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <optional>
#include <chrono>
#include <tuple>
#include <variant>

// Forward declarations for build_inventory_item_msg and v2 builders
namespace hb::item { class item_system; struct item; }
namespace hb { class item_registry; }
namespace hb::inventory { struct inventory_entry; }

namespace hb::network
{

// Attack type enum
enum class attack_type : uint8_t
{
    regular = 0, // Normal melee attack
    dash = 1,    // Dash attack (requires 100% skill, 1 tile gap)
    ranged = 2   // Ranged attack (bow/crossbow)
};

// Projectile type for ranged attack broadcasts
enum class projectile_type : uint8_t
{
    none = 0,
    arrow = 1,
    poison_arrow = 2,
};

// Target type enum
enum class target_type : uint8_t
{
    none = 0,
    player = 1,
    npc = 2,
    ground = 3, // For ground-targeted spells/skills
    item = 4    // For items on ground
};

// Message types for the JSON protocol
enum class json_message_type
{
    // System
    error,
    ping,
    pong,

    // Authentication (Wave 0)
    login_request,
    login_response,
    logout_request,
    logout_response,
    create_account_request,
    create_account_response,

    // Character selection
    get_characters_request,
    get_characters_response,
    create_character_request,
    create_character_response,
    delete_character_request,
    delete_character_response,
    enter_game_request,
    enter_game_response,

    // Game state data (sent after enter_game)
    character_data,
    inventory_data,
    equipment_data,
    skills_data,
    entity_spawn,
    entity_despawn,

    // In-game movement and position
    player_move_request,
    player_move_response,
    player_stop_request,
    player_stop_response,
    player_position_update, // Broadcast to nearby players

    // Combat
    player_attack_request,
    player_attack_response,
    combat_attack_broadcast, // Broadcast attack to nearby players
    entity_hp_update,        // HP changed (damage or heal)
    entity_death,            // Entity died
    combat_effect,           // Visual combat/spell effect broadcast (damage, heal, miss, etc.)

    // Actions
    player_magic_request,
    player_magic_response,
    player_skill_request,
    player_skill_response,
    skill_update,    // Server broadcast: skill level changed
    skill_progress,  // Server->client: skill SSN progress update
    player_pickup_request,
    player_pickup_response,
    player_interact_request,
    player_interact_response,

    // Chat
    chat_message,           // Chat message (local, shout, guild, party, whisper, etc.)
    chat_message_broadcast, // Outbound chat broadcast to recipients

    // Commands (separate from chat - client-side /command construct)
    command_request,  // Client sends a command
    command_response, // Server response to command

    // Teleportation
    map_teleporters,   // Full teleporter list for a map
    teleporter_update, // Live add/remove/modify teleporter
    player_teleport,   // Sent to player being teleported

    // View/Resolution
    set_view_range,    // Client updates visibility radius
    set_render_mode,   // Server tells client which rendering mode to use
    view_range_update, // Server tells client its effective visibility range

    // NPC messages (server -> client)
    npc_spawn,   // NPC appears in view
    npc_despawn, // NPC leaves view
    npc_move,    // NPC moved
    npc_attack,  // NPC attacked something

    // Ground item messages (server -> client)
    ground_item_spawn,   // Item appeared on ground
    ground_item_removed, // Item picked up from ground

    // Player state updates (server -> client)
    player_death_info,  // Death details sent to dead player
    hunger_update,      // Player hunger level changed
    environment_update, // Day/night cycle and weather state

    // Entity info (client -> server -> client)
    entity_info_request,  // Request info about an entity
    entity_info_response, // Entity info response

    // Equipment
    player_equip_request,
    player_equip_response,
    player_unequip_request,
    player_unequip_response,
    equipment_change_broadcast,
    stat_update,
    spell_list_update,
    experience_update,

    // Dynamic objects - ground fields (server -> client)
    dynamic_object_spawn,
    dynamic_object_removed,

    // NPC interaction - shops
    shop_buy_request,
    shop_buy_response,
    shop_sell_request,
    shop_sell_response,
    shop_sell_confirm_request,
    shop_sell_confirm_response,
    shop_repair_request,
    shop_repair_response,
    shop_repair_confirm_request,
    shop_repair_confirm_response,

    // NPC interaction - banking
    bank_deposit_request,
    bank_deposit_response,
    bank_withdraw_request,
    bank_withdraw_response,

    // NPC interaction - dialog
    dialog_choice_request,
    dialog_choice_response,

    // Party
    party_invite_request,
    party_invite_response,
    party_invite_notice,
    party_accept_request,
    party_accept_response,
    party_leave_request,
    party_leave_response,
    party_update,

    // Crafting - manufacturing
    manufacture_list_request,
    manufacture_list_response,
    manufacture_request,
    manufacture_response,

    // Crafting - alchemy
    alchemy_list_request,
    alchemy_list_response,
    alchemy_request,
    alchemy_response,

    // Mining
    mine_request,
    mine_response,
    mineral_spawn,
    mineral_despawn,

    // Fishing
    fish_skill_request,     // Player activates fishing skill
    fish_skill_response,    // Skill activated or error
    fish_engaged,           // Fish found, show dialog with preview
    fish_chance_update,     // Periodic update of catch %
    fish_catch_request,     // Player attempts catch ("Try Now!")
    fish_catch_response,    // Success/fail/canceled result
    fish_spawn_broadcast,   // Fish appeared on map
    fish_despawn_broadcast, // Fish removed from map

    // Death/Respawn
    respawn_request,
    respawn_response,

    // Admin web tool
    enter_admin_mode_request,
    enter_admin_mode_response,
    admin_server_stats_request,
    admin_server_stats_response,
    admin_list_players_request,
    admin_list_players_response,
    admin_get_player_request,
    admin_get_player_response,
    admin_kick_player_request,
    admin_kick_player_response,
    admin_ban_player_request,
    admin_ban_player_response,
    admin_teleport_player_request,
    admin_teleport_player_response,
    admin_modify_player_request,
    admin_modify_player_response,
    admin_list_maps_request,
    admin_list_maps_response,
    admin_get_map_request,
    admin_get_map_response,
    admin_spawn_npc_request,
    admin_spawn_npc_response,
    admin_kill_npc_request,
    admin_kill_npc_response,
    admin_get_inventory_request,
    admin_get_inventory_response,
    admin_give_item_request,
    admin_give_item_response,
    admin_remove_item_request,
    admin_remove_item_response,
    admin_list_guilds_request,
    admin_list_guilds_response,
    admin_get_guild_request,
    admin_get_guild_response,
    admin_get_account_request,
    admin_get_account_response,
    admin_unban_player_request,
    admin_unban_player_response,
    admin_subscribe_map_request,
    admin_subscribe_map_response,
    admin_subscribe_player_request,
    admin_subscribe_player_response,
    admin_unsubscribe_request,
    admin_unsubscribe_response,
    admin_spectator_init,
    admin_get_map_data_request,
    admin_get_map_data_response,
    admin_player_connected,
    admin_player_disconnected,
    admin_chat_log,

    // Admin web tool - expanded
    admin_broadcast_request,
    admin_broadcast_response,
    admin_mute_player_request,
    admin_mute_player_response,
    admin_unmute_player_request,
    admin_unmute_player_response,
    admin_list_item_templates_request,
    admin_list_item_templates_response,
    admin_get_item_template_request,
    admin_get_item_template_response,
    admin_list_npc_templates_request,
    admin_list_npc_templates_response,
    admin_get_npc_template_request,
    admin_get_npc_template_response,
    admin_get_war_status_request,
    admin_get_war_status_response,
    admin_list_parties_request,
    admin_list_parties_response,
    admin_search_players_request,
    admin_search_players_response,

    // Admin web tool - phase 3
    admin_get_audit_log_request,
    admin_get_audit_log_response,
    admin_get_config_request,
    admin_get_config_response,
    admin_set_config_request,
    admin_set_config_response,
    admin_reload_config_request,
    admin_reload_config_response,
    admin_list_scheduled_tasks_request,
    admin_list_scheduled_tasks_response,
    admin_cancel_scheduled_task_request,
    admin_cancel_scheduled_task_response,
    admin_run_query_request,
    admin_run_query_response,
    admin_list_map_npcs_request,
    admin_list_map_npcs_response,
    admin_list_map_ground_items_request,
    admin_list_map_ground_items_response,
    admin_remove_ground_item_request,
    admin_remove_ground_item_response,
    admin_guild_action_request,
    admin_guild_action_response,
    admin_message_player_request,
    admin_message_player_response,
    admin_set_environment_request,
    admin_set_environment_response,
    admin_shutdown_server_request,
    admin_shutdown_server_response,

    // Admin web tool - phase 4
    admin_modify_skills_request,
    admin_modify_skills_response,
    admin_modify_spells_request,
    admin_modify_spells_response,
    admin_get_player_quests_request,
    admin_get_player_quests_response,
    admin_quest_action_request,
    admin_quest_action_response,
    admin_remove_effects_request,
    admin_remove_effects_response,
    admin_create_account_request,
    admin_create_account_response,
    admin_change_password_request,
    admin_change_password_response,
    admin_set_admin_level_request,
    admin_set_admin_level_response,
    admin_list_spawn_points_request,
    admin_list_spawn_points_response,
    admin_list_spell_templates_request,
    admin_list_spell_templates_response,
    admin_get_spell_template_request,
    admin_get_spell_template_response,
    admin_set_maintenance_mode_request,
    admin_set_maintenance_mode_response,
    admin_create_character_request_admin,
    admin_create_character_response_admin,
    admin_delete_character_request_admin,
    admin_delete_character_response_admin,
    admin_manage_ip_bans_request,
    admin_manage_ip_bans_response,

    // Admin web tool - task registry
    admin_start_task_request,
    admin_start_task_response,

    // Admin web tool - performance stats
    admin_perf_stats_request,
    admin_perf_stats_response,

    // Friend system
    friend_request_send_request,
    friend_request_send_response,
    friend_request_accept_request,
    friend_request_accept_response,
    friend_request_decline_request,
    friend_request_decline_response,
    friend_request_cancel_request,
    friend_request_cancel_response,
    friend_remove_request,
    friend_remove_response,
    friend_block_request,
    friend_block_response,
    friend_unblock_request,
    friend_unblock_response,
    friend_list_request,
    friend_list_response,
    friend_request_notification,
    friend_accepted_notification,
    friend_online_notification,
    friend_offline_notification,

    // Crusade warfare
    crusade_started,                   // S->C: Crusade has begun
    crusade_ended,                     // S->C: Crusade is over
    crusade_status_update,             // S->C: Periodic status to participants
    select_duty_request,               // C->S: Player selects crusade duty
    select_duty_response,              // S->C: Duty selection result
    crusade_strike_point_update,       // S->C: Strike point HP changes
    crusade_meteor_warning,            // S->C: Meteor incoming
    crusade_meteor_hit,                // S->C: Meteor impact results
    crusade_meteor_result,             // S->C: Full meteor event summary
    crusade_mana_update,               // S->C: Mana pool status (to commanders)
    crusade_construction_point_update, // S->C: Construction point change
    summon_war_unit_request,           // C->S: Summon a war structure
    summon_war_unit_response,          // S->C: Result of summon attempt
    crusade_map_status,                // S->C: War structure overview for commanders

    // Heldenian warfare
    heldenian_started,       // S->C: Heldenian war started
    heldenian_ended,         // S->C: Heldenian war ended
    heldenian_status_update, // S->C: Tower/door status update

    // Apocalypse event
    apocalypse_started,   // S->C: Apocalypse event started
    apocalypse_ended,     // S->C: Apocalypse event ended
    apocalypse_gate_open, // S->C: Gate open notification

    // Force recall
    force_recall_timer,   // S->C: Countdown timer in enemy territory
    force_recall_execute, // S->C: Player being recalled

    // War rewards
    crusade_reward_summary, // S->C: End-of-war reward summary to participant

    // Admin war management
    admin_start_war_request,         // C->S: Start a war event (admin)
    admin_start_war_response,        // S->C: Result
    admin_end_war_request,           // C->S: End a running war (admin)
    admin_end_war_response,          // S->C: Result
    admin_war_history_request,       // C->S: Get war history
    admin_war_history_response,      // S->C: War history list
    admin_war_participants_request,  // C->S: Get participants for a specific war
    admin_war_participants_response, // S->C: Participant list

    // Item audit log
    admin_item_log_request,  // C->S: Query item/gold audit logs
    admin_item_log_response, // S->C: Paginated log results

    // Crusade guild teleport
    crusade_set_guild_teleport_request,  // C->S: Commander sets guild teleport location
    crusade_set_guild_teleport_response, // S->C: Result
    crusade_guild_teleport_request,      // C->S: Member requests guild teleport
    crusade_guild_teleport_response,     // S->C: Teleport destination or error

    // Crusade mana collector MP restoration
    crusade_mp_restore, // S->C: Mana collector restored MP to nearby allies

    // Guild system (player-facing)
    guild_create_request,          // C->S: Create a guild
    guild_create_response,         // S->C: Creation result
    guild_disband_request,         // C->S: Disband guild
    guild_disband_response,        // S->C: Disband result
    guild_leave_request,           // C->S: Leave guild
    guild_leave_response,          // S->C: Leave result
    guild_kick_request,            // C->S: Kick member
    guild_kick_response,           // S->C: Kick result
    guild_invite_request,          // C->S: Invite player to guild
    guild_invite_response,         // S->C: Invite result
    guild_invite_received,         // S->C: Push invite notification to target
    guild_invite_respond_request,  // C->S: Accept/decline guild invite
    guild_invite_respond_response, // S->C: Respond result
    guild_promote_request,         // C->S: Promote member
    guild_promote_response,        // S->C: Promote result
    guild_demote_request,          // C->S: Demote member
    guild_demote_response,         // S->C: Demote result
    guild_set_motd_request,        // C->S: Set guild MOTD
    guild_set_motd_response,       // S->C: MOTD result
    guild_info_request,            // C->S: Get guild info
    guild_info_response,           // S->C: Guild info with member list
    guild_update,                  // S->C: Broadcast guild state change

    // Item usage
    player_use_item_request,  // C->S: Use a consumable item
    player_use_item_response, // S->C: Use item result

    // Command list (server -> client push)
    available_commands,          // S->C: Full command list on enter_game
    command_availability_update, // S->C: Partial update when state changes

    // Spell learning (gold -> spell, at an NPC that teaches it)
    learn_spell_request,  // C->S: Ask to learn a spell from a nearby teacher
    learn_spell_response, // S->C: Result plus remaining gold

    // Stat points (3 awarded per level, previously unspendable)
    stat_point_request,  // C->S: Spend one stat point on a stat
    stat_point_response, // S->C: Result plus remaining points

    // Combat mode
    combat_mode_change_request,   // C->S: Toggle combat mode
    combat_mode_change_response,  // S->C: Confirm combat mode change
    combat_mode_change_broadcast, // S->C: Broadcast combat mode change to nearby

    // Action broadcast (replaces legacy MSGID_EVENT_MOTION)
    player_action_broadcast, // S->C: Player performed an action (nearby see animation)

    // Item upgrade
    item_upgrade_request,  // C->S: Upgrade item with Xelima/Merien stone
    item_upgrade_response, // S->C: Upgrade result

    // Special ability
    activate_ability_request,  // C->S: Activate special weapon ability
    activate_ability_response, // S->C: Activation result
    special_ability_status,    // S->C: Ability status update (ready/active/cooldown/disabled)

    // Inventory management
    inventory_reposition_request, // C->S: Reposition item in inventory
    player_drop_item_request,     // C->S: Drop item from inventory
    player_drop_item_response,    // S->C: Drop result
    inventory_item_update,        // S->C: Single item added/changed
    inventory_item_removed,       // S->C: Item removed from inventory
    inventory_weight_update,      // S->C: Weight capacity changed

    // Gold notification
    gold_update, // S->C: Gold amount changed

    // Bank management
    bank_slot_update, // S->C: Single bank slot changed

    // v2 state update messages (item::serialize_item-based)
    inventory_item_add,    // S->C: Item added to inventory
    inventory_item_delta,  // S->C: Partial update (count/durability only)
    inventory_gold_update, // S->C: Gold total changed
    force_unequip,         // S->C: Server forced unequip (broken, hammer_strip, armor_break)
    equipment_change,      // S->broadcast: Equipment slot changed (visible to nearby)
    bank_slot_cleared,     // S->C: Bank slot emptied
    ability_activated,     // S->broadcast: Special ability activated on entity
    ability_expired,       // S->broadcast: Special ability expired on entity

    // v2 action messages (acknowledgment-only results)
    inventory_reposition,       // C->S: Reposition item in inventory grid (no response)
    equip_request,              // C->S: Equip item to slot (string-based)
    equip_result,               // S->C: Equip acknowledgment
    unequip_request,            // C->S: Unequip slot (string-based)
    unequip_result,             // S->C: Unequip acknowledgment
    pickup_request,             // C->S: Pick up ground item
    pickup_result,              // S->C: Pickup acknowledgment
    drop_request,               // C->S: Drop item from inventory
    drop_result,                // S->C: Drop acknowledgment
    use_item_request,           // C->S: Use consumable item
    use_item_result,            // S->C: Use item acknowledgment
    upgrade_request,            // C->S: Upgrade item with stone
    upgrade_result,             // S->C: Upgrade acknowledgment
    shop_buy_request_v2,        // C->S: Buy from shop (v2)
    shop_buy_result,            // S->C: Buy acknowledgment
    shop_sell_request_v2,       // C->S: Sell to shop (v2)
    shop_sell_result,           // S->C: Sell acknowledgment
    shop_repair_request_v2,     // C->S: Repair at shop (v2)
    shop_repair_result,         // S->C: Repair acknowledgment
    bank_deposit_request_v2,    // C->S: Deposit to bank (v2)
    bank_deposit_result,        // S->C: Deposit acknowledgment
    bank_withdraw_request_v2,   // C->S: Withdraw from bank (v2)
    bank_withdraw_result,       // S->C: Withdraw acknowledgment
    bank_reposition_request,    // C->S: Move item within bank
    bank_reposition_result,     // S->C: Reposition acknowledgment
    activate_ability_request_v2, // C->S: Activate special ability (v2)
    activate_ability_failed,    // S->C: Ability activation failed

    // v2 trade messages (3-phase: offer -> lock -> confirm -> exchange)
    trade_request,    // C->S: Initiate trade with target
    trade_invite,     // S->C: Incoming trade invitation
    trade_accept,     // C->S: Accept trade invitation
    trade_decline,    // C->S: Decline trade invitation
    trade_opened,     // S->C: Trade window opened
    trade_add_item,   // C->S: Add item to trade offer
    trade_remove_item,// C->S: Remove item from trade offer
    trade_set_gold,   // C->S: Set gold amount in trade
    trade_update,     // S->C: Trade offer state changed
    trade_lock,       // C->S: Lock trade offer
    trade_lock_status,// S->C: Lock status of both sides
    trade_confirm,    // C->S: Confirm locked trade
    trade_complete,   // S->C: Trade completed
    trade_cancel,     // C->S: Cancel trade
    trade_canceled,   // S->C: Trade was canceled

    // NPC interaction - shop/bank open (v2, serialize_item-based)
    shop_open,      // S->C: Shop inventory display
    bank_open,      // S->C: Bank contents display

    // Party loot distribution
    set_loot_rule,     // C->S: Set party loot rule
    loot_rule_changed, // S->C: Loot rule was changed
    loot_roll,         // C->S: Roll on loot item
    loot_pass,         // C->S: Pass on loot item
    loot_assign,       // C->S: Master looter assigns item
    loot_available,    // S->C: Loot is available for distribution
    loot_roll_result,  // S->C: Someone rolled on loot
    loot_awarded,      // S->C: Loot was awarded to someone
    loot_expired,      // S->C: Loot expired without being awarded

    // Unknown/invalid
    unknown
};

// Convert message type to string
[[nodiscard]] constexpr auto to_string(json_message_type type) -> std::string_view
{
    switch (type)
    {
    case json_message_type::error:
        return "error";
    case json_message_type::ping:
        return "ping";
    case json_message_type::pong:
        return "pong";
    case json_message_type::login_request:
        return "login_request";
    case json_message_type::login_response:
        return "login_response";
    case json_message_type::logout_request:
        return "logout_request";
    case json_message_type::logout_response:
        return "logout_response";
    case json_message_type::create_account_request:
        return "create_account_request";
    case json_message_type::create_account_response:
        return "create_account_response";
    case json_message_type::get_characters_request:
        return "get_characters_request";
    case json_message_type::get_characters_response:
        return "get_characters_response";
    case json_message_type::create_character_request:
        return "create_character_request";
    case json_message_type::create_character_response:
        return "create_character_response";
    case json_message_type::delete_character_request:
        return "delete_character_request";
    case json_message_type::delete_character_response:
        return "delete_character_response";
    case json_message_type::enter_game_request:
        return "enter_game_request";
    case json_message_type::enter_game_response:
        return "enter_game_response";
    case json_message_type::character_data:
        return "character_data";
    case json_message_type::inventory_data:
        return "inventory_data";
    case json_message_type::equipment_data:
        return "equipment_data";
    case json_message_type::skills_data:
        return "skills_data";
    case json_message_type::entity_spawn:
        return "entity_spawn";
    case json_message_type::entity_despawn:
        return "entity_despawn";
    case json_message_type::player_move_request:
        return "player_move_request";
    case json_message_type::player_move_response:
        return "player_move_response";
    case json_message_type::player_stop_request:
        return "player_stop_request";
    case json_message_type::player_stop_response:
        return "player_stop_response";
    case json_message_type::player_position_update:
        return "player_position_update";
    case json_message_type::player_attack_request:
        return "player_attack_request";
    case json_message_type::player_attack_response:
        return "player_attack_response";
    case json_message_type::combat_attack_broadcast:
        return "combat_attack_broadcast";
    case json_message_type::entity_hp_update:
        return "entity_hp_update";
    case json_message_type::entity_death:
        return "entity_death";
    case json_message_type::combat_effect:
        return "combat_effect";
    case json_message_type::player_magic_request:
        return "player_magic_request";
    case json_message_type::player_magic_response:
        return "player_magic_response";
    case json_message_type::player_skill_request:
        return "player_skill_request";
    case json_message_type::player_skill_response:
        return "player_skill_response";
    case json_message_type::skill_update:
        return "skill_update";
    case json_message_type::skill_progress:
        return "skill_progress";
    case json_message_type::player_pickup_request:
        return "player_pickup_request";
    case json_message_type::player_pickup_response:
        return "player_pickup_response";
    case json_message_type::player_interact_request:
        return "player_interact_request";
    case json_message_type::player_interact_response:
        return "player_interact_response";
    case json_message_type::chat_message:
        return "chat_message";
    case json_message_type::chat_message_broadcast:
        return "chat_message_broadcast";
    case json_message_type::command_request:
        return "command_request";
    case json_message_type::command_response:
        return "command_response";
    case json_message_type::map_teleporters:
        return "map_teleporters";
    case json_message_type::teleporter_update:
        return "teleporter_update";
    case json_message_type::player_teleport:
        return "player_teleport";
    case json_message_type::set_view_range:
        return "set_view_range";
    case json_message_type::set_render_mode:
        return "set_render_mode";
    case json_message_type::view_range_update:
        return "view_range_update";
    case json_message_type::npc_spawn:
        return "npc_spawn";
    case json_message_type::npc_despawn:
        return "npc_despawn";
    case json_message_type::npc_move:
        return "npc_move";
    case json_message_type::npc_attack:
        return "npc_attack";
    case json_message_type::ground_item_spawn:
        return "ground_item_spawn";
    case json_message_type::ground_item_removed:
        return "ground_item_removed";
    case json_message_type::player_death_info:
        return "player_death_info";
    case json_message_type::hunger_update:
        return "hunger_update";
    case json_message_type::environment_update:
        return "environment_update";
    case json_message_type::entity_info_request:
        return "entity_info_request";
    case json_message_type::entity_info_response:
        return "entity_info_response";
    case json_message_type::player_equip_request:
        return "player_equip_request";
    case json_message_type::player_equip_response:
        return "player_equip_response";
    case json_message_type::player_unequip_request:
        return "player_unequip_request";
    case json_message_type::player_unequip_response:
        return "player_unequip_response";
    case json_message_type::equipment_change_broadcast:
        return "equipment_change_broadcast";
    case json_message_type::stat_update:
        return "stat_update";
    case json_message_type::spell_list_update:
        return "spell_list_update";
    case json_message_type::experience_update:
        return "experience_update";
    case json_message_type::dynamic_object_spawn:
        return "dynamic_object_spawn";
    case json_message_type::dynamic_object_removed:
        return "dynamic_object_removed";
    case json_message_type::shop_buy_request:
        return "shop_buy_request";
    case json_message_type::shop_buy_response:
        return "shop_buy_response";
    case json_message_type::shop_sell_request:
        return "shop_sell_request";
    case json_message_type::shop_sell_response:
        return "shop_sell_response";
    case json_message_type::shop_sell_confirm_request:
        return "shop_sell_confirm_request";
    case json_message_type::shop_sell_confirm_response:
        return "shop_sell_confirm_response";
    case json_message_type::shop_repair_request:
        return "shop_repair_request";
    case json_message_type::shop_repair_response:
        return "shop_repair_response";
    case json_message_type::shop_repair_confirm_request:
        return "shop_repair_confirm_request";
    case json_message_type::shop_repair_confirm_response:
        return "shop_repair_confirm_response";
    case json_message_type::bank_deposit_request:
        return "bank_deposit_request";
    case json_message_type::bank_deposit_response:
        return "bank_deposit_response";
    case json_message_type::bank_withdraw_request:
        return "bank_withdraw_request";
    case json_message_type::bank_withdraw_response:
        return "bank_withdraw_response";
    case json_message_type::dialog_choice_request:
        return "dialog_choice_request";
    case json_message_type::dialog_choice_response:
        return "dialog_choice_response";
    case json_message_type::party_invite_request:
        return "party_invite_request";
    case json_message_type::party_invite_response:
        return "party_invite_response";
    case json_message_type::party_invite_notice:
        return "party_invite_notice";
    case json_message_type::party_accept_request:
        return "party_accept_request";
    case json_message_type::party_accept_response:
        return "party_accept_response";
    case json_message_type::party_leave_request:
        return "party_leave_request";
    case json_message_type::party_leave_response:
        return "party_leave_response";
    case json_message_type::party_update:
        return "party_update";
    case json_message_type::manufacture_list_request:
        return "manufacture_list_request";
    case json_message_type::manufacture_list_response:
        return "manufacture_list_response";
    case json_message_type::manufacture_request:
        return "manufacture_request";
    case json_message_type::manufacture_response:
        return "manufacture_response";
    case json_message_type::alchemy_list_request:
        return "alchemy_list_request";
    case json_message_type::alchemy_list_response:
        return "alchemy_list_response";
    case json_message_type::alchemy_request:
        return "alchemy_request";
    case json_message_type::alchemy_response:
        return "alchemy_response";
    case json_message_type::mine_request:
        return "mine_request";
    case json_message_type::mine_response:
        return "mine_response";
    case json_message_type::mineral_spawn:
        return "mineral_spawn";
    case json_message_type::mineral_despawn:
        return "mineral_despawn";
    case json_message_type::fish_skill_request:
        return "fish_skill_request";
    case json_message_type::fish_skill_response:
        return "fish_skill_response";
    case json_message_type::fish_engaged:
        return "fish_engaged";
    case json_message_type::fish_chance_update:
        return "fish_chance_update";
    case json_message_type::fish_catch_request:
        return "fish_catch_request";
    case json_message_type::fish_catch_response:
        return "fish_catch_response";
    case json_message_type::fish_spawn_broadcast:
        return "fish_spawn_broadcast";
    case json_message_type::fish_despawn_broadcast:
        return "fish_despawn_broadcast";
    case json_message_type::respawn_request:
        return "respawn_request";
    case json_message_type::respawn_response:
        return "respawn_response";
    case json_message_type::enter_admin_mode_request:
        return "enter_admin_mode_request";
    case json_message_type::enter_admin_mode_response:
        return "enter_admin_mode_response";
    case json_message_type::admin_server_stats_request:
        return "admin_server_stats_request";
    case json_message_type::admin_server_stats_response:
        return "admin_server_stats_response";
    case json_message_type::admin_list_players_request:
        return "admin_list_players_request";
    case json_message_type::admin_list_players_response:
        return "admin_list_players_response";
    case json_message_type::admin_get_player_request:
        return "admin_get_player_request";
    case json_message_type::admin_get_player_response:
        return "admin_get_player_response";
    case json_message_type::admin_kick_player_request:
        return "admin_kick_player_request";
    case json_message_type::admin_kick_player_response:
        return "admin_kick_player_response";
    case json_message_type::admin_ban_player_request:
        return "admin_ban_player_request";
    case json_message_type::admin_ban_player_response:
        return "admin_ban_player_response";
    case json_message_type::admin_teleport_player_request:
        return "admin_teleport_player_request";
    case json_message_type::admin_teleport_player_response:
        return "admin_teleport_player_response";
    case json_message_type::admin_modify_player_request:
        return "admin_modify_player_request";
    case json_message_type::admin_modify_player_response:
        return "admin_modify_player_response";
    case json_message_type::admin_list_maps_request:
        return "admin_list_maps_request";
    case json_message_type::admin_list_maps_response:
        return "admin_list_maps_response";
    case json_message_type::admin_get_map_request:
        return "admin_get_map_request";
    case json_message_type::admin_get_map_response:
        return "admin_get_map_response";
    case json_message_type::admin_spawn_npc_request:
        return "admin_spawn_npc_request";
    case json_message_type::admin_spawn_npc_response:
        return "admin_spawn_npc_response";
    case json_message_type::admin_kill_npc_request:
        return "admin_kill_npc_request";
    case json_message_type::admin_kill_npc_response:
        return "admin_kill_npc_response";
    case json_message_type::admin_get_inventory_request:
        return "admin_get_inventory_request";
    case json_message_type::admin_get_inventory_response:
        return "admin_get_inventory_response";
    case json_message_type::admin_give_item_request:
        return "admin_give_item_request";
    case json_message_type::admin_give_item_response:
        return "admin_give_item_response";
    case json_message_type::admin_remove_item_request:
        return "admin_remove_item_request";
    case json_message_type::admin_remove_item_response:
        return "admin_remove_item_response";
    case json_message_type::admin_list_guilds_request:
        return "admin_list_guilds_request";
    case json_message_type::admin_list_guilds_response:
        return "admin_list_guilds_response";
    case json_message_type::admin_get_guild_request:
        return "admin_get_guild_request";
    case json_message_type::admin_get_guild_response:
        return "admin_get_guild_response";
    case json_message_type::admin_get_account_request:
        return "admin_get_account_request";
    case json_message_type::admin_get_account_response:
        return "admin_get_account_response";
    case json_message_type::admin_unban_player_request:
        return "admin_unban_player_request";
    case json_message_type::admin_unban_player_response:
        return "admin_unban_player_response";
    case json_message_type::admin_subscribe_map_request:
        return "admin_subscribe_map_request";
    case json_message_type::admin_subscribe_map_response:
        return "admin_subscribe_map_response";
    case json_message_type::admin_get_map_data_request:
        return "admin_get_map_data_request";
    case json_message_type::admin_get_map_data_response:
        return "admin_get_map_data_response";
    case json_message_type::admin_subscribe_player_request:
        return "admin_subscribe_player_request";
    case json_message_type::admin_subscribe_player_response:
        return "admin_subscribe_player_response";
    case json_message_type::admin_unsubscribe_request:
        return "admin_unsubscribe_request";
    case json_message_type::admin_unsubscribe_response:
        return "admin_unsubscribe_response";
    case json_message_type::admin_spectator_init:
        return "admin_spectator_init";
    case json_message_type::admin_player_connected:
        return "admin_player_connected";
    case json_message_type::admin_player_disconnected:
        return "admin_player_disconnected";
    case json_message_type::admin_chat_log:
        return "admin_chat_log";
    case json_message_type::admin_broadcast_request:
        return "admin_broadcast_request";
    case json_message_type::admin_broadcast_response:
        return "admin_broadcast_response";
    case json_message_type::admin_mute_player_request:
        return "admin_mute_player_request";
    case json_message_type::admin_mute_player_response:
        return "admin_mute_player_response";
    case json_message_type::admin_unmute_player_request:
        return "admin_unmute_player_request";
    case json_message_type::admin_unmute_player_response:
        return "admin_unmute_player_response";
    case json_message_type::admin_list_item_templates_request:
        return "admin_list_item_templates_request";
    case json_message_type::admin_list_item_templates_response:
        return "admin_list_item_templates_response";
    case json_message_type::admin_get_item_template_request:
        return "admin_get_item_template_request";
    case json_message_type::admin_get_item_template_response:
        return "admin_get_item_template_response";
    case json_message_type::admin_list_npc_templates_request:
        return "admin_list_npc_templates_request";
    case json_message_type::admin_list_npc_templates_response:
        return "admin_list_npc_templates_response";
    case json_message_type::admin_get_npc_template_request:
        return "admin_get_npc_template_request";
    case json_message_type::admin_get_npc_template_response:
        return "admin_get_npc_template_response";
    case json_message_type::admin_get_war_status_request:
        return "admin_get_war_status_request";
    case json_message_type::admin_get_war_status_response:
        return "admin_get_war_status_response";
    case json_message_type::admin_list_parties_request:
        return "admin_list_parties_request";
    case json_message_type::admin_list_parties_response:
        return "admin_list_parties_response";
    case json_message_type::admin_search_players_request:
        return "admin_search_players_request";
    case json_message_type::admin_search_players_response:
        return "admin_search_players_response";
    case json_message_type::admin_get_audit_log_request:
        return "admin_get_audit_log_request";
    case json_message_type::admin_get_audit_log_response:
        return "admin_get_audit_log_response";
    case json_message_type::admin_get_config_request:
        return "admin_get_config_request";
    case json_message_type::admin_get_config_response:
        return "admin_get_config_response";
    case json_message_type::admin_set_config_request:
        return "admin_set_config_request";
    case json_message_type::admin_set_config_response:
        return "admin_set_config_response";
    case json_message_type::admin_reload_config_request:
        return "admin_reload_config_request";
    case json_message_type::admin_reload_config_response:
        return "admin_reload_config_response";
    case json_message_type::admin_list_scheduled_tasks_request:
        return "admin_list_scheduled_tasks_request";
    case json_message_type::admin_list_scheduled_tasks_response:
        return "admin_list_scheduled_tasks_response";
    case json_message_type::admin_cancel_scheduled_task_request:
        return "admin_cancel_scheduled_task_request";
    case json_message_type::admin_cancel_scheduled_task_response:
        return "admin_cancel_scheduled_task_response";
    case json_message_type::admin_run_query_request:
        return "admin_run_query_request";
    case json_message_type::admin_run_query_response:
        return "admin_run_query_response";
    case json_message_type::admin_list_map_npcs_request:
        return "admin_list_map_npcs_request";
    case json_message_type::admin_list_map_npcs_response:
        return "admin_list_map_npcs_response";
    case json_message_type::admin_list_map_ground_items_request:
        return "admin_list_map_ground_items_request";
    case json_message_type::admin_list_map_ground_items_response:
        return "admin_list_map_ground_items_response";
    case json_message_type::admin_remove_ground_item_request:
        return "admin_remove_ground_item_request";
    case json_message_type::admin_remove_ground_item_response:
        return "admin_remove_ground_item_response";
    case json_message_type::admin_guild_action_request:
        return "admin_guild_action_request";
    case json_message_type::admin_guild_action_response:
        return "admin_guild_action_response";
    case json_message_type::admin_message_player_request:
        return "admin_message_player_request";
    case json_message_type::admin_message_player_response:
        return "admin_message_player_response";
    case json_message_type::admin_set_environment_request:
        return "admin_set_environment_request";
    case json_message_type::admin_set_environment_response:
        return "admin_set_environment_response";
    case json_message_type::admin_shutdown_server_request:
        return "admin_shutdown_server_request";
    case json_message_type::admin_shutdown_server_response:
        return "admin_shutdown_server_response";
    case json_message_type::admin_modify_skills_request:
        return "admin_modify_skills_request";
    case json_message_type::admin_modify_skills_response:
        return "admin_modify_skills_response";
    case json_message_type::admin_modify_spells_request:
        return "admin_modify_spells_request";
    case json_message_type::admin_modify_spells_response:
        return "admin_modify_spells_response";
    case json_message_type::admin_get_player_quests_request:
        return "admin_get_player_quests_request";
    case json_message_type::admin_get_player_quests_response:
        return "admin_get_player_quests_response";
    case json_message_type::admin_quest_action_request:
        return "admin_quest_action_request";
    case json_message_type::admin_quest_action_response:
        return "admin_quest_action_response";
    case json_message_type::admin_remove_effects_request:
        return "admin_remove_effects_request";
    case json_message_type::admin_remove_effects_response:
        return "admin_remove_effects_response";
    case json_message_type::admin_create_account_request:
        return "admin_create_account_request";
    case json_message_type::admin_create_account_response:
        return "admin_create_account_response";
    case json_message_type::admin_change_password_request:
        return "admin_change_password_request";
    case json_message_type::admin_change_password_response:
        return "admin_change_password_response";
    case json_message_type::admin_set_admin_level_request:
        return "admin_set_admin_level_request";
    case json_message_type::admin_set_admin_level_response:
        return "admin_set_admin_level_response";
    case json_message_type::admin_list_spawn_points_request:
        return "admin_list_spawn_points_request";
    case json_message_type::admin_list_spawn_points_response:
        return "admin_list_spawn_points_response";
    case json_message_type::admin_list_spell_templates_request:
        return "admin_list_spell_templates_request";
    case json_message_type::admin_list_spell_templates_response:
        return "admin_list_spell_templates_response";
    case json_message_type::admin_get_spell_template_request:
        return "admin_get_spell_template_request";
    case json_message_type::admin_get_spell_template_response:
        return "admin_get_spell_template_response";
    case json_message_type::admin_set_maintenance_mode_request:
        return "admin_set_maintenance_mode_request";
    case json_message_type::admin_set_maintenance_mode_response:
        return "admin_set_maintenance_mode_response";
    case json_message_type::admin_create_character_request_admin:
        return "admin_create_character_request_admin";
    case json_message_type::admin_create_character_response_admin:
        return "admin_create_character_response_admin";
    case json_message_type::admin_delete_character_request_admin:
        return "admin_delete_character_request_admin";
    case json_message_type::admin_delete_character_response_admin:
        return "admin_delete_character_response_admin";
    case json_message_type::admin_manage_ip_bans_request:
        return "admin_manage_ip_bans_request";
    case json_message_type::admin_manage_ip_bans_response:
        return "admin_manage_ip_bans_response";
    case json_message_type::admin_start_task_request:
        return "admin_start_task_request";
    case json_message_type::admin_start_task_response:
        return "admin_start_task_response";
    case json_message_type::admin_perf_stats_request:
        return "admin_perf_stats_request";
    case json_message_type::admin_perf_stats_response:
        return "admin_perf_stats_response";
    case json_message_type::friend_request_send_request:
        return "friend_request_send_request";
    case json_message_type::friend_request_send_response:
        return "friend_request_send_response";
    case json_message_type::friend_request_accept_request:
        return "friend_request_accept_request";
    case json_message_type::friend_request_accept_response:
        return "friend_request_accept_response";
    case json_message_type::friend_request_decline_request:
        return "friend_request_decline_request";
    case json_message_type::friend_request_decline_response:
        return "friend_request_decline_response";
    case json_message_type::friend_request_cancel_request:
        return "friend_request_cancel_request";
    case json_message_type::friend_request_cancel_response:
        return "friend_request_cancel_response";
    case json_message_type::friend_remove_request:
        return "friend_remove_request";
    case json_message_type::friend_remove_response:
        return "friend_remove_response";
    case json_message_type::friend_block_request:
        return "friend_block_request";
    case json_message_type::friend_block_response:
        return "friend_block_response";
    case json_message_type::friend_unblock_request:
        return "friend_unblock_request";
    case json_message_type::friend_unblock_response:
        return "friend_unblock_response";
    case json_message_type::friend_list_request:
        return "friend_list_request";
    case json_message_type::friend_list_response:
        return "friend_list_response";
    case json_message_type::friend_request_notification:
        return "friend_request_notification";
    case json_message_type::friend_accepted_notification:
        return "friend_accepted_notification";
    case json_message_type::friend_online_notification:
        return "friend_online_notification";
    case json_message_type::friend_offline_notification:
        return "friend_offline_notification";
    case json_message_type::crusade_started:
        return "crusade_started";
    case json_message_type::crusade_ended:
        return "crusade_ended";
    case json_message_type::crusade_status_update:
        return "crusade_status_update";
    case json_message_type::select_duty_request:
        return "select_duty_request";
    case json_message_type::select_duty_response:
        return "select_duty_response";
    case json_message_type::crusade_strike_point_update:
        return "crusade_strike_point_update";
    case json_message_type::crusade_meteor_warning:
        return "crusade_meteor_warning";
    case json_message_type::crusade_meteor_hit:
        return "crusade_meteor_hit";
    case json_message_type::crusade_meteor_result:
        return "crusade_meteor_result";
    case json_message_type::crusade_mana_update:
        return "crusade_mana_update";
    case json_message_type::crusade_construction_point_update:
        return "crusade_construction_point_update";
    case json_message_type::summon_war_unit_request:
        return "summon_war_unit_request";
    case json_message_type::summon_war_unit_response:
        return "summon_war_unit_response";
    case json_message_type::crusade_map_status:
        return "crusade_map_status";
    case json_message_type::heldenian_started:
        return "heldenian_started";
    case json_message_type::heldenian_ended:
        return "heldenian_ended";
    case json_message_type::heldenian_status_update:
        return "heldenian_status_update";
    case json_message_type::apocalypse_started:
        return "apocalypse_started";
    case json_message_type::apocalypse_ended:
        return "apocalypse_ended";
    case json_message_type::apocalypse_gate_open:
        return "apocalypse_gate_open";
    case json_message_type::force_recall_timer:
        return "force_recall_timer";
    case json_message_type::force_recall_execute:
        return "force_recall_execute";
    case json_message_type::crusade_reward_summary:
        return "crusade_reward_summary";
    case json_message_type::admin_start_war_request:
        return "admin_start_war_request";
    case json_message_type::admin_start_war_response:
        return "admin_start_war_response";
    case json_message_type::admin_end_war_request:
        return "admin_end_war_request";
    case json_message_type::admin_end_war_response:
        return "admin_end_war_response";
    case json_message_type::admin_war_history_request:
        return "admin_war_history_request";
    case json_message_type::admin_war_history_response:
        return "admin_war_history_response";
    case json_message_type::admin_war_participants_request:
        return "admin_war_participants_request";
    case json_message_type::admin_war_participants_response:
        return "admin_war_participants_response";
    case json_message_type::admin_item_log_request:
        return "admin_item_log_request";
    case json_message_type::admin_item_log_response:
        return "admin_item_log_response";
    case json_message_type::crusade_set_guild_teleport_request:
        return "crusade_set_guild_teleport_request";
    case json_message_type::crusade_set_guild_teleport_response:
        return "crusade_set_guild_teleport_response";
    case json_message_type::crusade_guild_teleport_request:
        return "crusade_guild_teleport_request";
    case json_message_type::crusade_guild_teleport_response:
        return "crusade_guild_teleport_response";
    case json_message_type::crusade_mp_restore:
        return "crusade_mp_restore";
    case json_message_type::guild_create_request:
        return "guild_create_request";
    case json_message_type::guild_create_response:
        return "guild_create_response";
    case json_message_type::guild_disband_request:
        return "guild_disband_request";
    case json_message_type::guild_disband_response:
        return "guild_disband_response";
    case json_message_type::guild_leave_request:
        return "guild_leave_request";
    case json_message_type::guild_leave_response:
        return "guild_leave_response";
    case json_message_type::guild_kick_request:
        return "guild_kick_request";
    case json_message_type::guild_kick_response:
        return "guild_kick_response";
    case json_message_type::guild_invite_request:
        return "guild_invite_request";
    case json_message_type::guild_invite_response:
        return "guild_invite_response";
    case json_message_type::guild_invite_received:
        return "guild_invite_received";
    case json_message_type::guild_invite_respond_request:
        return "guild_invite_respond_request";
    case json_message_type::guild_invite_respond_response:
        return "guild_invite_respond_response";
    case json_message_type::guild_promote_request:
        return "guild_promote_request";
    case json_message_type::guild_promote_response:
        return "guild_promote_response";
    case json_message_type::guild_demote_request:
        return "guild_demote_request";
    case json_message_type::guild_demote_response:
        return "guild_demote_response";
    case json_message_type::guild_set_motd_request:
        return "guild_set_motd_request";
    case json_message_type::guild_set_motd_response:
        return "guild_set_motd_response";
    case json_message_type::guild_info_request:
        return "guild_info_request";
    case json_message_type::guild_info_response:
        return "guild_info_response";
    case json_message_type::guild_update:
        return "guild_update";
    case json_message_type::player_use_item_request:
        return "player_use_item_request";
    case json_message_type::player_use_item_response:
        return "player_use_item_response";
    case json_message_type::available_commands:
        return "available_commands";
    case json_message_type::command_availability_update:
        return "command_availability_update";
    case json_message_type::learn_spell_request:
        return "learn_spell_request";
    case json_message_type::learn_spell_response:
        return "learn_spell_response";
    case json_message_type::stat_point_request:
        return "stat_point_request";
    case json_message_type::stat_point_response:
        return "stat_point_response";
    case json_message_type::combat_mode_change_request:
        return "combat_mode_change_request";
    case json_message_type::combat_mode_change_response:
        return "combat_mode_change_response";
    case json_message_type::combat_mode_change_broadcast:
        return "combat_mode_change_broadcast";
    case json_message_type::player_action_broadcast:
        return "player_action_broadcast";
    case json_message_type::item_upgrade_request:
        return "item_upgrade_request";
    case json_message_type::item_upgrade_response:
        return "item_upgrade_response";
    case json_message_type::activate_ability_request:
        return "activate_ability_request";
    case json_message_type::activate_ability_response:
        return "activate_ability_response";
    case json_message_type::special_ability_status:
        return "special_ability_status";
    case json_message_type::inventory_reposition_request:
        return "inventory_reposition_request";
    case json_message_type::player_drop_item_request:
        return "player_drop_item_request";
    case json_message_type::player_drop_item_response:
        return "player_drop_item_response";
    case json_message_type::inventory_item_update:
        return "inventory_item_update";
    case json_message_type::inventory_item_removed:
        return "inventory_item_removed";
    case json_message_type::inventory_weight_update:
        return "inventory_weight_update";
    case json_message_type::gold_update:
        return "gold_update";
    case json_message_type::bank_slot_update:
        return "bank_slot_update";
    case json_message_type::inventory_item_add:
        return "inventory_item_add";
    case json_message_type::inventory_item_delta:
        return "inventory_item_delta";
    case json_message_type::inventory_gold_update:
        return "inventory_gold_update";
    case json_message_type::force_unequip:
        return "force_unequip";
    case json_message_type::equipment_change:
        return "equipment_change";
    case json_message_type::bank_slot_cleared:
        return "bank_slot_cleared";
    case json_message_type::ability_activated:
        return "ability_activated";
    case json_message_type::ability_expired:
        return "ability_expired";
    case json_message_type::inventory_reposition:
        return "inventory_reposition";
    case json_message_type::equip_request:
        return "equip_request";
    case json_message_type::equip_result:
        return "equip_result";
    case json_message_type::unequip_request:
        return "unequip_request";
    case json_message_type::unequip_result:
        return "unequip_result";
    case json_message_type::pickup_request:
        return "pickup_request";
    case json_message_type::pickup_result:
        return "pickup_result";
    case json_message_type::drop_request:
        return "drop_request";
    case json_message_type::drop_result:
        return "drop_result";
    case json_message_type::use_item_request:
        return "use_item_request";
    case json_message_type::use_item_result:
        return "use_item_result";
    case json_message_type::upgrade_request:
        return "upgrade_request";
    case json_message_type::upgrade_result:
        return "upgrade_result";
    case json_message_type::shop_buy_request_v2:
        return "shop_buy_request_v2";
    case json_message_type::shop_buy_result:
        return "shop_buy_result";
    case json_message_type::shop_sell_request_v2:
        return "shop_sell_request_v2";
    case json_message_type::shop_sell_result:
        return "shop_sell_result";
    case json_message_type::shop_repair_request_v2:
        return "shop_repair_request_v2";
    case json_message_type::shop_repair_result:
        return "shop_repair_result";
    case json_message_type::bank_deposit_request_v2:
        return "bank_deposit_request_v2";
    case json_message_type::bank_deposit_result:
        return "bank_deposit_result";
    case json_message_type::bank_withdraw_request_v2:
        return "bank_withdraw_request_v2";
    case json_message_type::bank_withdraw_result:
        return "bank_withdraw_result";
    case json_message_type::bank_reposition_request:
        return "bank_reposition_request";
    case json_message_type::bank_reposition_result:
        return "bank_reposition_result";
    case json_message_type::activate_ability_request_v2:
        return "activate_ability_request_v2";
    case json_message_type::activate_ability_failed:
        return "activate_ability_failed";
    case json_message_type::trade_request:
        return "trade_request";
    case json_message_type::trade_invite:
        return "trade_invite";
    case json_message_type::trade_accept:
        return "trade_accept";
    case json_message_type::trade_decline:
        return "trade_decline";
    case json_message_type::trade_opened:
        return "trade_opened";
    case json_message_type::trade_add_item:
        return "trade_add_item";
    case json_message_type::trade_remove_item:
        return "trade_remove_item";
    case json_message_type::trade_set_gold:
        return "trade_set_gold";
    case json_message_type::trade_update:
        return "trade_update";
    case json_message_type::trade_lock:
        return "trade_lock";
    case json_message_type::trade_lock_status:
        return "trade_lock_status";
    case json_message_type::trade_confirm:
        return "trade_confirm";
    case json_message_type::trade_complete:
        return "trade_complete";
    case json_message_type::trade_cancel:
        return "trade_cancel";
    case json_message_type::trade_canceled:
        return "trade_canceled";
    case json_message_type::shop_open:
        return "shop_open";
    case json_message_type::bank_open:
        return "bank_open";
    case json_message_type::set_loot_rule:
        return "set_loot_rule";
    case json_message_type::loot_rule_changed:
        return "loot_rule_changed";
    case json_message_type::loot_roll:
        return "loot_roll";
    case json_message_type::loot_pass:
        return "loot_pass";
    case json_message_type::loot_assign:
        return "loot_assign";
    case json_message_type::loot_available:
        return "loot_available";
    case json_message_type::loot_roll_result:
        return "loot_roll_result";
    case json_message_type::loot_awarded:
        return "loot_awarded";
    case json_message_type::loot_expired:
        return "loot_expired";
    default:
        return "unknown";
    }
}

// Parse message type from string
[[nodiscard]] auto parse_message_type(std::string_view type_str) -> json_message_type;

// Base JSON message structure
struct json_message
{
    json_message_type type{json_message_type::unknown};
    uint32_t seq{0}; // Sequence number for request/response matching
    nlohmann::json data;
    std::string raw_type; // Original type string from JSON (useful for debugging unknown types)

    [[nodiscard]] auto to_json() const -> nlohmann::json;
    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<json_message, std::string>;
    [[nodiscard]] static auto parse(std::string_view json_str) -> result<json_message, std::string>;
};

// Request message structures

struct login_request_data
{
    std::string username;
    std::string password;    // normal login (empty when using forum_token)
    std::string forum_token; // forum token-based auto-login (empty when using password)

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<login_request_data, std::string>;
};

struct create_account_request_data
{
    std::string username;
    std::string password;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<create_account_request_data, std::string>;
};

struct create_character_request_data
{
    std::string name;
    int16_t class_type{0};
    int16_t nation{0};
    int16_t gender{1}; // 1 = male, 2 = female
    int16_t hair_style{0};
    int16_t hair_color{0};
    int16_t skin_color{0};
    int16_t underwear_color{0};
    std::optional<int16_t> strength;
    std::optional<int16_t> dexterity;
    std::optional<int16_t> vitality;
    std::optional<int16_t> intelligence;
    std::optional<int16_t> magic;
    std::optional<int16_t> charisma;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<create_character_request_data, std::string>;
    [[nodiscard]] auto to_create_info() const -> auth::character_create_info;
};

struct delete_character_request_data
{
    uint32_t character_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<delete_character_request_data, std::string>;
};

struct enter_game_request_data
{
    uint32_t character_id{0};
    bool force_disconnect{false}; // If true, disconnect existing session for this account
    int16_t screen_width{640};    // Client effective viewport width for visibility calculation
    int16_t screen_height{480};   // Client effective viewport height for visibility calculation

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<enter_game_request_data, std::string>;
};

// Set view range request from client (when resolution or view mode changes)
struct set_view_range_request_data
{
    int16_t screen_width{640};  // Effective viewport width
    int16_t screen_height{480}; // Effective viewport height

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<set_view_range_request_data, std::string>;
};

// Movement request from client
struct player_move_request_data
{
    int16_t x{0}; // Current position for validation
    int16_t y{0};
    int16_t direction{0};          // Direction to move (0-7)
    bool is_running{false};        // True if running (moves 2 tiles), false if walking (1 tile)
    uint64_t timestamp{0};         // Client timestamp in ms
    std::optional<int16_t> dest_x; // Mouse destination tile X (optional)
    std::optional<int16_t> dest_y; // Mouse destination tile Y (optional)

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_move_request_data, std::string>;
};

// Stop request from client
struct player_stop_request_data
{
    int16_t x{0}; // Position where stopped
    int16_t y{0};
    std::optional<int16_t> direction; // Direction to face (0-7), optional
    uint64_t timestamp{0};            // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_stop_request_data, std::string>;
};

// Attack request from client
struct player_attack_request_data
{
    int16_t x{0}; // Current position for validation
    int16_t y{0};
    int16_t direction{0}; // Direction facing
    attack_type type{attack_type::regular};
    target_type tgt_type{target_type::none};
    uint32_t target_id{0}; // Target entity ID
    uint64_t timestamp{0}; // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_attack_request_data, std::string>;
};

// Magic cast request from client
struct player_magic_request_data
{
    int16_t x{0}; // Current position for validation
    int16_t y{0};
    int16_t direction{0}; // Direction facing
    uint32_t spell_id{0}; // Spell to cast
    target_type tgt_type{target_type::none};
    uint32_t target_id{0}; // Target entity ID (for targeted spells)
    int16_t target_x{0};   // Target location (for ground-targeted spells)
    int16_t target_y{0};
    uint64_t timestamp{0}; // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_magic_request_data, std::string>;
};

// Skill use request from client
struct player_skill_request_data
{
    int16_t x{0}; // Current position for validation
    int16_t y{0};
    int16_t direction{0}; // Direction facing
    uint32_t skill_id{0}; // Skill to use
    target_type tgt_type{target_type::none};
    uint32_t target_id{0}; // Target entity ID (if applicable)
    uint64_t timestamp{0}; // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_skill_request_data, std::string>;
};

// Pickup item request from client
struct player_pickup_request_data
{
    int16_t x{0}; // Current position for validation
    int16_t y{0};
    uint32_t item_id{0};   // Ground item ID to pick up
    uint64_t timestamp{0}; // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_pickup_request_data, std::string>;
};

// Interact request from client (NPC dialog, objects, etc.)
struct player_interact_request_data
{
    int16_t x{0}; // Current position for validation
    int16_t y{0};
    target_type tgt_type{target_type::none};
    uint32_t target_id{0}; // Target NPC or object ID
    uint64_t timestamp{0}; // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_interact_request_data, std::string>;
};

// Chat channel type for JSON protocol
enum class chat_channel_type : uint8_t
{
    local = 0,   // Nearby players (default, no prefix)
    shout = 1,   // Server-wide (! prefix)
    guild = 2,   // Guild members (@ prefix)
    party = 3,   // Party members ($ prefix)
    whisper = 4, // Private message (recipient name or # prefix)
    global = 5,  // Global channel
    trade = 6,   // Trade channel
    faction = 7, // Faction channel (Aresden/Elvine)
    system = 8,  // System messages (server-generated only)
};

// Chat message request from client
// Client can send either:
//   - Raw message with prefix: "!Hello everyone" -> shout
//   - Explicit channel: {"channel": "guild", "content": "Hello guild"}
struct chat_message_request_data
{
    std::string content;                       // Message content (may include prefix)
    std::optional<std::string> channel;        // Explicit channel override
    std::optional<std::string> recipient_name; // For whispers
    uint64_t timestamp{0};                     // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<chat_message_request_data, std::string>;
};

// Chat broadcast data (sent to recipients)
struct chat_message_broadcast_data
{
    std::string channel;                       // "local", "shout", "guild", "party", "whisper", etc.
    uint32_t sender_id{0};                     // Sender player ID (0 for system)
    std::string sender_name;                   // Sender display name
    std::string content;                       // Message content
    std::vector<std::string> flags;            // "emote", "censored", "system", "gm"
    std::string timestamp;                     // ISO 8601 timestamp
    std::optional<std::string> recipient_name; // For whisper (recipient sees their own name)

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Command request from client (for /commands)
// Commands are sent as structured data, not parsed from chat
struct command_request_data
{
    std::string command;           // Command name (without /)
    std::vector<std::string> args; // Command arguments
    nlohmann::json params;         // Named parameters (optional)
    uint64_t timestamp{0};         // Client timestamp in ms

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<command_request_data, std::string>;
};

// Command response to client
struct command_response_data
{
    bool success{false};
    std::string command;   // Echo back the command
    std::string message;   // Success/error message
    nlohmann::json result; // Command-specific result data

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Equipment visual data for a single slot in entity spawns and character data
struct equip_visual_msg
{
    int8_t appr{0};
    int8_t color{0};
    std::string name;   // Item name for tooltips
    std::string rarity; // "common".."ancient"
};

// Character data message (full character info for entering game)
struct character_data_msg
{
    uint32_t id;
    std::string name;
    int16_t level;
    int16_t class_type;
    int16_t nation;
    int16_t gender;
    std::string map_name;
    int16_t pos_x;
    int16_t pos_y;
    int32_t hp;
    int32_t hp_max;
    int32_t mp;
    int32_t mp_max;
    int32_t sp;
    int32_t sp_max;
    int32_t gold;
    int16_t str;
    int16_t dex;
    int16_t vit;
    int16_t int_;
    int16_t mag;
    int16_t cha;
    int16_t hair_style;
    int16_t hair_color;
    int16_t skin_color;
    int64_t experience;
    int32_t pk_count;
    int16_t stat_points{0}; // pontos por gastar; sem isto o cliente so descobre no proximo level up
    int32_t hunger_level;
    std::string guild_name;
    std::string guild_tag;
    uint8_t guild_rank{0};

    // Equipment visuals (appr values for rendering equipped items)
    equip_visual_msg weapon_visual;
    equip_visual_msg shield_visual;
    equip_visual_msg body_visual;
    equip_visual_msg pants_visual;
    equip_visual_msg head_visual;
    equip_visual_msg arms_visual;
    equip_visual_msg boots_visual;
    equip_visual_msg cape_visual;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Inventory item for inventory_data message
struct inventory_item_msg
{
    uint32_t item_id;
    std::string name;
    int16_t count;
    int16_t durability;
    int16_t max_durability;
    item::item_attribute attribute{}; // Per-instance attribute data

    // Template-derived fields for client rendering
    uint8_t item_type{0};       // item_type enum as int
    uint8_t equip_pos{0};       // item_equip_pos enum as int
    int16_t sprite{0};          // legacy m_sSprite: sprite ID
    int16_t sprite_frame{0};    // legacy m_sSpriteFrame: subframe within sprite
    int8_t color{0};            // legacy m_cItemColor: tint index
    int16_t weight{0};
    int16_t level_limit{0};

    // Free-form pixel position (client layout)
    int16_t pos_x{0};
    int16_t pos_y{0};
    int32_t z_order{0};

    // Equipment: if set, item is equipped in this slot (maps to equip_slot enum)
    std::optional<uint8_t> equipped_slot{};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Active buff info for entity spawns
struct buff_info_msg
{
    std::string type; // spell_effect_type as string
    uint32_t spell_id{0};
    int32_t magnitude{0};
    int64_t remaining_ms{0};
};

struct visible_entity_msg
{
    uint32_t entity_id;
    std::string type; // "player" or "npc"
    std::string name;
    int16_t x;
    int16_t y;
    int16_t hp_percent;
    int16_t direction;

    // Player-specific fields (optional, only used when type == "player")
    std::string faction;     // "neutral", "aresden", "elvine"
    std::string hostility;   // "enemy", "friendly", "neutral" (relative to viewing player)
    std::string pk_status;   // "innocent", "criminal", "murderer"
    std::string guild_name;  // Player's guild name (empty if no guild)
    std::string guild_tag;   // Player's guild tag (empty if no guild)
    bool combat_mode{false}; // true = attack stance, false = peace mode

    // Player base appearance
    int8_t gender{0};          // 1=male, 2=female
    int8_t skin_color{0};      // 1-3
    int8_t hair_style{0};      // 0-7
    int8_t hair_color{0};      // 0-15
    int8_t underwear_color{0}; // 0-15
    int16_t player_level{0};   // Player level

    // Player equipment visuals (pre-computed)
    equip_visual_msg weapon_visual;
    equip_visual_msg shield_visual;
    equip_visual_msg body_visual;
    equip_visual_msg pants_visual;
    equip_visual_msg head_visual;
    equip_visual_msg arms_visual;
    equip_visual_msg boots_visual;
    equip_visual_msg cape_visual;

    int8_t weapon_glow{0};
    int8_t shield_glow{0};
    int8_t weapon_speed{0};

    // Status effects and buffs
    std::vector<std::string> status_effects;
    std::vector<buff_info_msg> active_buffs;

    // NPC-specific fields (optional, only used when type == "npc")
    uint32_t template_id{0};
    int16_t sprite_id{0}; // Legacy sprite type for client rendering (10=Slime, etc.)
    int16_t level{0};
    std::string category; // "monster", "guard", "merchant", etc.

    bool is_dead{false}; // Entity is a corpse

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Attack result for attack response
struct attack_result_msg
{
    bool hit{false};
    // hit=false cobre tanto um golpe que errou quanto dezenas de recusas (fora de
    // alcance, alvo morto, rapido demais). Sem separar os dois, o cliente nao consegue
    // medir taxa de acerto nenhuma. resolved=true significa que o ataque foi rolado.
    bool resolved{false};
    bool dodged{false}; // errou por esquiva do alvo (vs erro do atacante)
    bool critical{false};
    int32_t damage{0};
    uint32_t target_id{0};
    int16_t target_hp{0};     // Target's remaining HP
    int16_t target_hp_max{0}; // Target's max HP
    int16_t attacker_x{0};    // Confirmed attacker position
    int16_t attacker_y{0};

    // Ranged combat fields (optional)
    bool is_ranged{false};
    int32_t ammo_count{-1};       // Remaining arrows (-1 = not applicable)
    uint32_t ammo_template_id{0}; // Arrow template that was consumed

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Magic result for magic response
struct magic_result_msg
{
    bool success{false};
    uint32_t spell_id{0};
    int32_t mana_cost{0};
    int32_t damage{0};     // If damage spell
    int32_t heal{0};       // If heal spell
    uint32_t target_id{0}; // If targeted
    int16_t caster_mp{0};  // Remaining MP after cast

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Skill result for skill response
struct skill_result_msg
{
    bool success{false};
    uint32_t skill_id{0};
    int32_t effect_value{0}; // Skill-specific effect
    uint32_t target_id{0};   // If targeted

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Skill entry for skills_data message
struct skill_entry_msg
{
    uint8_t skill_id{0};
    int16_t level{0};
    int32_t total_uses{0};
    int32_t uses_this_level{0};
    int32_t uses_to_next_level{0};
};

// Pickup result for pickup response
struct pickup_result_msg
{
    bool success{false};
    uint32_t item_id{0};
    std::string item_name;
    int16_t quantity{0};
    item::item_attribute attribute{};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Interact result for interact response
struct interact_result_msg
{
    bool success{false};
    uint32_t target_id{0};
    std::string interaction_type;    // "dialog", "shop", "bank", etc.
    nlohmann::json interaction_data; // Type-specific data

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Teleporter info for map_teleporters message
struct teleporter_info_msg
{
    uint32_t id{0}; // Computed from position (x << 16 | y)
    int16_t x{0};
    int16_t y{0};
    std::string dest_map;
    int16_t dest_x{0};
    int16_t dest_y{0};
    int16_t dest_dir{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Map teleporters message (full list for a map)
struct map_teleporters_msg
{
    std::string map_name;
    std::vector<teleporter_info_msg> teleporters;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Teleporter update message (live add/remove/modify)
struct teleporter_update_msg
{
    std::string action; // "add", "remove", "modify"
    std::string map_name;
    teleporter_info_msg teleporter;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Player teleport message (sent to player being teleported)
struct player_teleport_msg
{
    std::string dest_map;
    int16_t dest_x{0};
    int16_t dest_y{0};
    int16_t dest_dir{0};
    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Response builders

[[nodiscard]] auto
make_error_response(uint32_t seq, std::string_view error_code, std::string_view message) -> json_message;

[[nodiscard]] auto make_login_response(uint32_t seq,
                                       bool success,
                                       std::optional<std::string_view> token = std::nullopt,
                                       std::optional<std::string_view> error = std::nullopt,
                                       std::optional<std::string_view> forum_token = std::nullopt) -> json_message;

[[nodiscard]] auto make_create_account_response(uint32_t seq,
                                                bool success,
                                                std::optional<uint32_t> account_id = std::nullopt,
                                                std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_logout_response(uint32_t seq, bool success) -> json_message;

[[nodiscard]] auto make_get_characters_response(uint32_t seq,
                                                const std::vector<auth::character_summary>& characters) -> json_message;

[[nodiscard]] auto make_create_character_response(uint32_t seq,
                                                  bool success,
                                                  std::optional<uint32_t> character_id = std::nullopt,
                                                  std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_delete_character_response(uint32_t seq,
                                                  bool success,
                                                  std::optional<std::string_view> error = std::nullopt) -> json_message;

// Known spell for enter_game_response
struct known_spell_msg
{
    uint16_t spell_id{0};
    int16_t level{1};
    int32_t total_casts{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Quest objective progress for enter_game_response
struct quest_objective_msg
{
    uint16_t id{0};
    uint8_t status{0}; // 0=incomplete, 1=complete, 2=failed
    int32_t current{0};
    int32_t required{1};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Active quest for enter_game_response
struct active_quest_msg
{
    uint16_t quest_id{0};
    uint8_t status{0}; // quest_status enum
    std::vector<quest_objective_msg> objectives;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Full game state for enter_game_response
struct game_state_msg
{
    character_data_msg character;
    std::vector<inventory_item_msg> inventory;
    std::vector<skill_entry_msg> skills;
    std::vector<known_spell_msg> spells;
    std::vector<active_quest_msg> quests;
    std::vector<uint16_t> completed_quests;
    int32_t gold{0};

    // Environment state
    uint8_t time_hour{12};
    uint8_t time_minute{0};
    bool is_day{true};
    uint8_t weather{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_enter_game_response(uint32_t seq,
                                            bool success,
                                            const game_state_msg* game_state = nullptr,
                                            std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto
make_inventory_data(uint32_t seq, const std::vector<inventory_item_msg>& items, int32_t gold) -> json_message;

[[nodiscard]] auto make_skills_data(uint32_t seq, const std::vector<skill_entry_msg>& skills) -> json_message;

[[nodiscard]] auto make_skill_update(uint32_t player_id_val,
                                     const skill_entry_msg& skill,
                                     int16_t old_level) -> json_message;

[[nodiscard]] auto make_skill_progress(uint8_t skill_id,
                                       int32_t uses_this_level,
                                       int32_t uses_to_next_level,
                                       uint8_t percent) -> json_message;

[[nodiscard]] auto make_entity_spawn(uint32_t seq, const visible_entity_msg& entity) -> json_message;

[[nodiscard]] auto make_entity_despawn(uint32_t seq, uint32_t entity_id) -> json_message;

// Movement messages
[[nodiscard]] auto make_player_move_response(uint32_t seq,
                                             bool success,
                                             int16_t x,
                                             int16_t y,
                                             int16_t direction,
                                             std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_player_stop_response(uint32_t seq,
                                             bool success,
                                             int16_t x,
                                             int16_t y,
                                             int16_t direction,
                                             std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_player_position_update(uint32_t entity_id,
                                               int16_t x,
                                               int16_t y,
                                               int16_t direction,
                                               bool is_running = false,
                                               std::optional<int16_t> dest_x = std::nullopt,
                                               std::optional<int16_t> dest_y = std::nullopt) -> json_message;

// Combat messages
[[nodiscard]] auto make_player_attack_response(uint32_t seq,
                                               bool success,
                                               const attack_result_msg* result = nullptr,
                                               std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_combat_attack_broadcast(uint32_t attacker_id,
                                                uint32_t target_id,
                                                int16_t attacker_x,
                                                int16_t attacker_y,
                                                int16_t target_x,
                                                int16_t target_y,
                                                int16_t direction,
                                                bool hit,
                                                bool critical,
                                                int32_t damage,
                                                projectile_type projectile = projectile_type::none) -> json_message;

[[nodiscard]] auto make_entity_hp_update(uint32_t entity_id, int32_t hp, int32_t hp_max) -> json_message;

[[nodiscard]] auto
make_entity_death(uint32_t victim_id, uint32_t killer_id, int16_t x, int16_t y, int32_t damage = 0) -> json_message;

// Combat effect broadcast data (unified visual feedback for all combat/spell events)
struct combat_effect_data
{
    uint32_t source_id{0};
    uint32_t target_id{0};
    std::string effect_type; // "damage","heal","miss","dodge","block","resist","buff","debuff"
    int32_t value{0};
    std::string damage_type;          // "physical","magic","fire","ice","lightning","poison","holy","dark","pure"
    std::optional<uint32_t> spell_id; // absent for melee
    bool is_critical{false};
    int16_t target_x{0};
    int16_t target_y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_combat_effect(const combat_effect_data& data) -> json_message;

// Magic messages
[[nodiscard]] auto make_player_magic_response(uint32_t seq,
                                              bool success,
                                              const magic_result_msg* result = nullptr,
                                              std::optional<std::string_view> error = std::nullopt) -> json_message;

// Skill messages
[[nodiscard]] auto make_player_skill_response(uint32_t seq,
                                              bool success,
                                              const skill_result_msg* result = nullptr,
                                              std::optional<std::string_view> error = std::nullopt) -> json_message;

// Pickup messages
[[nodiscard]] auto make_player_pickup_response(uint32_t seq,
                                               bool success,
                                               const pickup_result_msg* result = nullptr,
                                               std::optional<std::string_view> error = std::nullopt) -> json_message;

// Interact messages
[[nodiscard]] auto make_player_interact_response(uint32_t seq,
                                                 bool success,
                                                 const interact_result_msg* result = nullptr,
                                                 std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_pong_response(uint32_t seq) -> json_message;

// Chat messages
[[nodiscard]] auto make_chat_message_response(uint32_t seq,
                                              bool success,
                                              std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_chat_message_broadcast(const chat_message_broadcast_data& data) -> json_message;

// Command messages
[[nodiscard]] auto make_command_response(uint32_t seq,
                                         bool success,
                                         std::string_view command,
                                         std::string_view message,
                                         const nlohmann::json& result = nlohmann::json::object()) -> json_message;

// Teleportation messages
[[nodiscard]] auto make_map_teleporters(const map_teleporters_msg& data) -> json_message;

[[nodiscard]] auto make_teleporter_update(const teleporter_update_msg& data) -> json_message;

[[nodiscard]] auto make_player_teleport(uint32_t seq, const player_teleport_msg& data) -> json_message;

// NPC spawn data
struct npc_spawn_data
{
    uint32_t entity_id{0};
    uint32_t template_id{0};
    int16_t sprite_id{0}; // Legacy sprite type for client rendering (10=Slime, etc.)
    std::string name;
    int16_t x{0};
    int16_t y{0};
    uint8_t direction{0};
    int32_t hp{0};
    int32_t max_hp{0};
    int16_t level{0};
    std::string category;                  // "monster", "guard", "merchant", etc.
    std::string hostility;                 // "enemy", "friendly", "neutral" (relative to viewing player)
    std::vector<std::string> attributes;   // NPC attributes: "Poisonous", "Anti-Magic", etc.
    bool is_dead{false};                   // True for dead NPC corpses

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// NPC despawn data
struct npc_despawn_data
{
    uint32_t entity_id{0};
};

// NPC move data
struct npc_move_data
{
    uint32_t entity_id{0};
    int16_t x{0};
    int16_t y{0};
    uint8_t direction{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// NPC attack data
struct npc_attack_data
{
    uint32_t attacker_id{0};
    uint32_t target_id{0};
    int32_t damage{0};
    bool is_critical{false};
    bool is_ranged{false}; // NPC ranged attack (sends projectile visual)
    int16_t attacker_x{0};
    int16_t attacker_y{0};
    int16_t target_x{0};
    int16_t target_y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// NPC message builders
[[nodiscard]] auto make_npc_spawn_message(const npc_spawn_data& data) -> json_message;
[[nodiscard]] auto make_npc_despawn_message(uint32_t entity_id) -> json_message;
[[nodiscard]] auto make_npc_move_message(const npc_move_data& data) -> json_message;
[[nodiscard]] auto make_npc_attack_message(const npc_attack_data& data) -> json_message;

// Ground item spawn data (broadcast when item appears on ground)
struct ground_item_spawn_data
{
    uint32_t item_id{0};     // Item instance ID
    uint32_t template_id{0}; // Item template ID
    std::string item_name;   // Item name for display
    int16_t count{1};        // Stack count
    int16_t x{0};            // Position
    int16_t y{0};
    int16_t ground_sprite{0};        // Ground item sprite category (1=swords, 6=misc, etc.)
    int16_t ground_sprite_frame{0};  // Frame within ground sprite category
    int8_t item_color{0};            // Color tint index (0 = no tint)
    item::item_attribute attribute{}; // Per-instance attribute data
    std::string reason{"existing"};   // "drop" = live drop (play SFX), "existing" = already on ground

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Ground item spawn message builder
[[nodiscard]] auto make_ground_item_spawn(const ground_item_spawn_data& data) -> json_message;

// Ground item removed data (broadcast when item is picked up)
struct ground_item_removed_data
{
    uint32_t picker_id{0};   // Player who picked up the item
    std::string picker_name; // Picker's name for display
    uint32_t item_id{0};     // Item that was removed
    std::string item_name;   // Item name for display
    int16_t x{0};            // Position where item was
    int16_t y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Ground item message builder
[[nodiscard]] auto make_ground_item_removed(const ground_item_removed_data& data) -> json_message;

// Player death info data (sent to dead player)
struct player_death_info_data
{
    uint32_t killer_id{0};
    std::string killer_name;
    bool is_pvp{false};
    int64_t xp_lost{0};
    int32_t pk_points_change{0}; // Positive = killer gained PK points
    int32_t gold_reward{0};      // Gold earned for killing a PKer
    uint32_t respawn_delay_ms{0};
    std::string respawn_map;
    int16_t respawn_x{0};
    int16_t respawn_y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Player death info message builder
[[nodiscard]] auto make_player_death_info(const player_death_info_data& data) -> json_message;

// Hunger update data
struct hunger_update_data
{
    int8_t level{0};
    bool is_starving{false};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Hunger update message builder
[[nodiscard]] auto make_hunger_update(int8_t level) -> json_message;

// Environment update data (day/night + weather)
struct environment_update_data
{
    uint8_t hour{0};   // 0-23
    uint8_t minute{0}; // 0-59
    bool is_day{true};
    uint8_t weather{0}; // weather_type value (0-8)

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Environment update message builder
[[nodiscard]] auto make_environment_update(const environment_update_data& data) -> json_message;

// Entity info request data
struct entity_info_request_data
{
    uint32_t entity_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<entity_info_request_data, std::string>;
};

// Entity info response data
struct entity_info_response_data
{
    uint32_t entity_id{0};
    std::string entity_type; // "player" or "npc"
    std::string name;
    int16_t level{0};
    int32_t hp{0};
    int32_t hp_max{0};
    int16_t x{0};
    int16_t y{0};
    int16_t direction{0};

    // Player-specific fields
    std::optional<std::string> faction; // "aresden", "elvine", "neutral"
    std::optional<std::string> guild_name;
    std::optional<int16_t> class_type;
    std::optional<int32_t> pk_count;

    // NPC-specific fields
    std::optional<uint32_t> template_id;
    std::optional<int16_t> sprite_id;    // Legacy sprite type for client rendering
    std::optional<std::string> npc_type; // "monster", "vendor", "guard", etc.

    // Hostility (both player and NPC)
    std::optional<std::string> hostility; // "friendly", "hostile", "neutral"

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Entity info message builders
[[nodiscard]] auto make_entity_info_response(uint32_t seq,
                                             bool success,
                                             const entity_info_response_data* data = nullptr,
                                             std::optional<std::string_view> error = std::nullopt) -> json_message;

// === Equipment: Equip/Unequip request/response data ===

// Equip request from client
struct player_equip_request_data
{
    uint32_t item_id{0};    // Item to equip
    uint8_t target_slot{0}; // Target equip_slot

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_equip_request_data, std::string>;
};

// Unequip request from client
struct player_unequip_request_data
{
    uint8_t equip_slot{0}; // Slot to unequip

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<player_unequip_request_data, std::string>;
};

// Equipment change broadcast to nearby players
struct equipment_change_broadcast_data
{
    uint32_t entity_id{0}; // Player's ECS entity ID (client-facing)
    uint8_t slot{0};
    uint32_t item_id{0};     // 0 = now empty
    uint32_t template_id{0}; // For client sprite lookup

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Stat update sent to equipping player after equipment change
struct stat_update_data
{
    int32_t max_hp{0};
    int32_t max_mp{0};
    int32_t max_sp{0};
    int32_t attack_power{0};
    int32_t magic_power{0};
    int32_t defense{0};
    int32_t magic_defense{0};
    int32_t hit_rate{0};
    int32_t dodge_rate{0};
    int32_t critical_rate{0};
    int32_t max_weight{0};

    // Optional current vitals — included on teleport/respawn so client can resync
    std::optional<int32_t> hp;
    std::optional<int32_t> mp;
    std::optional<int32_t> sp;
    std::optional<int64_t> experience;
    std::optional<int32_t> gold;
    std::optional<uint8_t> level;
    std::optional<int32_t> pk_count;
    std::optional<uint8_t> hunger_level;
    std::optional<int32_t> contribution;
    std::optional<int32_t> enemy_kill_count;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Experience update sent to a player whenever they gain experience.
// Level-up fields are present only when the gain caused one or more level-ups.
struct experience_update_data
{
    int64_t experience_gained{0};
    int64_t experience{0};
    uint8_t level{0};

    // Present only on level-up
    std::optional<int32_t> levels_gained;
    std::optional<int32_t> max_hp;
    std::optional<int32_t> max_mp;
    std::optional<int32_t> max_sp;
    std::optional<int16_t> stat_points; // Total unspent stat points after the award

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Dynamic ground-field object appeared (broadcast to visible players)
struct dynamic_object_spawn_data
{
    uint16_t object_id{0};
    uint8_t object_type{0}; // dynamic_object_type value (1=fire, 8=icestorm, 9=spike, 10-12=poison cloud)
    int16_t x{0};
    int16_t y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Dynamic ground-field object expired/removed (broadcast to visible players)
struct dynamic_object_removed_data
{
    uint16_t object_id{0};
    uint8_t object_type{0};
    int16_t x{0};
    int16_t y{0};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Equipment message builders
[[nodiscard]] auto make_equipment_change_broadcast(const equipment_change_broadcast_data& data) -> json_message;
[[nodiscard]] auto make_stat_update(const stat_update_data& data) -> json_message;
[[nodiscard]] auto make_experience_update(const experience_update_data& data) -> json_message;
[[nodiscard]] auto make_dynamic_object_spawn(const dynamic_object_spawn_data& data) -> json_message;
[[nodiscard]] auto make_dynamic_object_removed(const dynamic_object_removed_data& data) -> json_message;

// Spell list update - sends full known spell list to client
[[nodiscard]] auto make_spell_list_update(const std::vector<known_spell_msg>& spells) -> json_message;

// === Inventory Reposition ===

struct inventory_reposition_request_data
{
    uint32_t item_id{0};
    int16_t pos_x{0};
    int16_t pos_y{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<inventory_reposition_request_data, std::string>;
};

// === Drop Item ===

struct drop_item_request_data
{
    uint32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<drop_item_request_data, std::string>;
};

[[nodiscard]] auto make_player_drop_item_response(uint32_t seq, bool success, std::string_view error = "") -> json_message;
[[nodiscard]] auto make_inventory_item_update(const inventory_item_msg& item) -> json_message;

struct learn_spell_request_data
{
    uint32_t npc_entity_id{0};
    uint16_t spell_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<learn_spell_request_data, std::string>;
};

[[nodiscard]] auto make_learn_spell_response(
    uint32_t seq, bool success, uint16_t spell_id, int64_t gold, std::string_view error) -> json_message;

// Stat point spending. stat: 0=str 1=dex 2=vit 3=int 4=mag 5=cha, matching
// player_system::add_stat_point.
struct stat_point_request_data
{
    int16_t stat{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<stat_point_request_data, std::string>;
};

[[nodiscard]] auto make_stat_point_response(
    uint32_t seq, bool success, int16_t stat, int16_t remaining, std::string_view error) -> json_message;
[[nodiscard]] auto make_inventory_item_removed(uint32_t item_id) -> json_message;
[[nodiscard]] auto make_inventory_weight_update(int32_t current_weight, int32_t max_weight) -> json_message;
[[nodiscard]] auto make_bank_slot_update(int16_t page, int16_t slot, const inventory_item_msg* item = nullptr) -> json_message;

// Gold update notification (server -> client)
struct gold_update_data
{
    int64_t gold{};     // New gold total after change
    int64_t change{};   // Amount changed (positive = gained, negative = spent)
    std::string reason; // Why: shop_buy, shop_sell, shop_repair, npc_loot, admin, trade

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_gold_update(const gold_update_data& data) -> json_message;

// === NPC Interaction: Shop request/response data ===

// Shop buy request from client
struct shop_buy_request_data
{
    uint32_t npc_entity_id{0};
    uint32_t item_template_id{0};
    int16_t count{1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_buy_request_data, std::string>;
};

// Shop sell request from client (requests a price quote)
struct shop_sell_request_data
{
    uint32_t npc_entity_id{0};
    uint32_t item_id{0};
    int16_t count{1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_sell_request_data, std::string>;
};

// Shop sell confirm request from client
struct shop_sell_confirm_request_data
{
    uint32_t npc_entity_id{0};
    uint32_t item_id{0};
    int16_t count{1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_sell_confirm_request_data, std::string>;
};

// Shop repair request from client (requests a cost quote)
struct shop_repair_request_data
{
    uint32_t npc_entity_id{0};
    uint32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_repair_request_data, std::string>;
};

// Shop repair confirm request from client
struct shop_repair_confirm_request_data
{
    uint32_t npc_entity_id{0};
    uint32_t item_id{0};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<shop_repair_confirm_request_data, std::string>;
};

// Bank deposit request from client
struct bank_deposit_request_data
{
    uint32_t npc_entity_id{0};
    uint32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<bank_deposit_request_data, std::string>;
};

// Bank withdraw request from client
struct bank_withdraw_request_data
{
    uint32_t npc_entity_id{0};
    int16_t bank_page{0};
    int16_t bank_slot{-1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<bank_withdraw_request_data, std::string>;
};

// Dialog choice request from client
struct dialog_choice_request_data
{
    uint32_t npc_entity_id{0};
    std::string node_id;
    int16_t choice_index{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<dialog_choice_request_data, std::string>;
};

// === Party messages ===

// Invite a player (by character name) to the sender's party.
// Creates the party implicitly when the sender is not in one.
struct party_invite_request_data
{
    std::string target_name;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<party_invite_request_data, std::string>;
};

// Accept (or decline) a pending party invite.
struct party_accept_request_data
{
    uint32_t party_id{0};
    bool accept{true};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<party_accept_request_data, std::string>;
};

[[nodiscard]] auto make_party_invite_response(uint32_t seq, bool success, uint32_t party_id, std::string_view error)
    -> json_message;
[[nodiscard]] auto make_party_invite_notice(uint32_t party_id, std::string_view inviter_name) -> json_message;
[[nodiscard]] auto make_party_accept_response(uint32_t seq, bool success, uint32_t party_id, std::string_view error)
    -> json_message;
[[nodiscard]] auto make_party_leave_response(uint32_t seq, bool success) -> json_message;
[[nodiscard]] auto make_party_update(uint32_t party_id,
                                     std::string_view leader_name,
                                     const std::vector<std::string>& member_names) -> json_message;

// === NPC Interaction response builders ===

// Shop buy response
[[nodiscard]] auto make_shop_buy_response(uint32_t seq,
                                          bool success,
                                          std::string_view item_name = "",
                                          int16_t count = 0,
                                          int32_t price_paid = 0,
                                          int64_t gold_remaining = 0,
                                          std::optional<std::string_view> error = std::nullopt) -> json_message;

// Shop sell quote response
[[nodiscard]] auto make_shop_sell_response(uint32_t seq,
                                           bool success,
                                           std::string_view item_name = "",
                                           int32_t offered_price = 0,
                                           int16_t durability = 0,
                                           std::optional<std::string_view> error = std::nullopt) -> json_message;

// Shop sell confirm response
[[nodiscard]] auto
make_shop_sell_confirm_response(uint32_t seq,
                                bool success,
                                int32_t gold_received = 0,
                                int64_t gold_total = 0,
                                std::optional<std::string_view> error = std::nullopt) -> json_message;

// Shop repair quote response
[[nodiscard]] auto make_shop_repair_response(uint32_t seq,
                                             bool success,
                                             std::string_view item_name = "",
                                             int32_t repair_cost = 0,
                                             int16_t durability = 0,
                                             std::optional<std::string_view> error = std::nullopt) -> json_message;

// Shop repair confirm response
[[nodiscard]] auto
make_shop_repair_confirm_response(uint32_t seq,
                                  bool success,
                                  int16_t new_durability = 0,
                                  int32_t gold_spent = 0,
                                  int64_t gold_remaining = 0,
                                  std::optional<std::string_view> error = std::nullopt) -> json_message;

// Bank deposit response
[[nodiscard]] auto make_bank_deposit_response(uint32_t seq,
                                              bool success,
                                              std::string_view item_name = "",
                                              std::optional<std::string_view> error = std::nullopt) -> json_message;

// Bank withdraw response
[[nodiscard]] auto make_bank_withdraw_response(uint32_t seq,
                                               bool success,
                                               std::string_view item_name = "",
                                               std::optional<std::string_view> error = std::nullopt) -> json_message;

// Dialog choice response - returns next dialog state or action result
struct dialog_option_msg
{
    std::string label;
    std::string action;
    std::string next_node;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_dialog_choice_response(uint32_t seq,
                                               bool success,
                                               std::string_view action = "",
                                               std::string_view node_id = "",
                                               std::string_view text = "",
                                               const std::vector<dialog_option_msg>& options = {},
                                               std::optional<std::string_view> error = std::nullopt) -> json_message;

// === View Mode & Visibility ===

// Rendering mode controlled by the server
// scaled: fixed internal resolution scaled to display (everyone sees same area)
// extended: native res, but entities only render within fair zone (terrain visible outside)
// special: unrestricted native res with zoom (commander/admin)
enum class view_mode : uint8_t
{
    scaled = 0,
    extended = 1,
    special = 2
};

[[nodiscard]] constexpr auto to_string(view_mode mode) -> std::string_view
{
    switch (mode)
    {
    case view_mode::scaled:
        return "scaled";
    case view_mode::extended:
        return "extended";
    case view_mode::special:
        return "special";
    default:
        return "scaled";
    }
}

[[nodiscard]] inline auto parse_view_mode(std::string_view str) -> view_mode
{
    if (str == "extended")
        return view_mode::extended;
    if (str == "special")
        return view_mode::special;
    return view_mode::scaled;
}

// Render mode message data (server -> client)
struct render_mode_data
{
    view_mode mode{view_mode::scaled};
    int16_t fair_width{800};  // Fair zone width in pixels
    int16_t fair_height{600}; // Fair zone height in pixels

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_set_render_mode(const render_mode_data& data) -> json_message;
[[nodiscard]] auto make_view_range_update(int16_t radius_x, int16_t radius_y, bool sees_all) -> json_message;

// Visibility range constants
inline constexpr int16_t min_visibility_radius = 15;
inline constexpr int16_t max_visibility_radius = 80;
inline constexpr float visibility_buffer_ratio = 0.2f; // 20% proportional buffer
inline constexpr int pixels_per_tile = 32;

// Separate X/Y visibility radii matching the rectangular viewport
struct visibility_radii
{
    int16_t x;
    int16_t y;
};

// Calculate visibility radii from effective viewport dimensions.
// Each axis is computed independently: (tiles/2) + buffer, clamped to [min, max].
// Client sends actual screen resolution for normal play, or max effective viewport
// (screen_res / min_zoom) when in a mode that allows zooming out further.
[[nodiscard]] inline auto calculate_visibility_radius(int16_t screen_width, int16_t screen_height) -> visibility_radii
{
    auto tiles_wide = static_cast<float>(screen_width) / pixels_per_tile;
    auto tiles_high = static_cast<float>(screen_height) / pixels_per_tile;

    auto base_x = tiles_wide / 2.0f;
    auto base_y = tiles_high / 2.0f;

    auto buffer_x = std::max(5.0f, base_x * visibility_buffer_ratio);
    auto buffer_y = std::max(5.0f, base_y * visibility_buffer_ratio);

    auto rx = static_cast<int16_t>(base_x + buffer_x);
    auto ry = static_cast<int16_t>(base_y + buffer_y);

    return {std::clamp(rx, min_visibility_radius, max_visibility_radius),
            std::clamp(ry, min_visibility_radius, max_visibility_radius)};
}

// === Crafting: Manufacturing request/response data ===

// Manufacture request from client
struct manufacture_request_data
{
    int32_t recipe_index{-1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<manufacture_request_data, std::string>;
};

// Alchemy request from client
struct alchemy_request_data
{
    int32_t recipe_id{-1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<alchemy_request_data, std::string>;
};

// Crafting response builders
[[nodiscard]] auto make_manufacture_list_response(uint32_t seq, const nlohmann::json& recipes) -> json_message;

[[nodiscard]] auto make_manufacture_response(uint32_t seq,
                                             bool success,
                                             std::string_view item_name = "",
                                             std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_alchemy_list_response(uint32_t seq, const nlohmann::json& recipes) -> json_message;

[[nodiscard]] auto make_alchemy_response(uint32_t seq,
                                         bool success,
                                         std::string_view item_name = "",
                                         std::optional<std::string_view> error = std::nullopt) -> json_message;

// === Death/Respawn response data ===

// Respawn response sent to client after successful respawn
struct respawn_response_data
{
    bool success{false};
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
    std::string error;

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_respawn_response(uint32_t seq,
                                         bool success,
                                         std::string_view map_name = "",
                                         int16_t x = 0,
                                         int16_t y = 0,
                                         std::optional<std::string_view> error = std::nullopt) -> json_message;

// === Mining request/response data ===

// Mine request from client
struct mine_request_data
{
    int16_t target_x{};
    int16_t target_y{};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<mine_request_data, std::string>;
};

// Mining response builders
[[nodiscard]] auto make_mine_response(uint32_t seq,
                                      bool success,
                                      std::string_view item_name = "",
                                      int32_t template_id = 0,
                                      bool node_depleted = false,
                                      std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_mineral_spawn(uint32_t node_id, uint8_t mineral_type, int16_t x, int16_t y) -> json_message;

[[nodiscard]] auto make_mineral_despawn(uint32_t node_id, int16_t x, int16_t y) -> json_message;

// === Fishing request/response data ===

// Fish skill response (ack or error)
[[nodiscard]] auto make_fish_skill_response(uint32_t seq,
                                            bool success,
                                            std::optional<std::string_view> error = std::nullopt) -> json_message;

// Fish engaged: fish found, show preview dialog
[[nodiscard]] auto make_fish_engaged(entity_id player_eid,
                                     std::string_view fish_name,
                                     uint8_t visual_type,
                                     int32_t initial_chance) -> json_message;

// Fish chance update: periodic catch % update
[[nodiscard]] auto make_fish_chance_update(entity_id player_eid, int32_t catch_chance) -> json_message;

// Fish catch result: success/fail/canceled
[[nodiscard]] auto make_fish_catch_response(entity_id player_eid,
                                            std::string_view result_str,
                                            std::string_view item_name = "",
                                            int32_t template_id = 0) -> json_message;

// Fish spawn broadcast
[[nodiscard]] auto
make_fish_spawn_broadcast(uint32_t fish_index, uint8_t visual_type, int16_t x, int16_t y) -> json_message;

// Fish despawn broadcast
[[nodiscard]] auto make_fish_despawn_broadcast(uint32_t fish_index, int16_t x, int16_t y) -> json_message;

// === Friend system data structures and builders ===

struct friend_target_request_data
{
    std::string target_name;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<friend_target_request_data, std::string>;
};

// Friend list response builder
[[nodiscard]] auto
make_friend_response(uint32_t seq, json_message_type type, bool success, std::string_view error = "") -> json_message;

struct friend_list_entry_msg
{
    std::string name;
    bool is_online{false};
};

struct friend_request_msg
{
    std::string name;
    bool is_outgoing{false};
};

[[nodiscard]] auto make_friend_list_response(uint32_t seq,
                                             const std::vector<friend_list_entry_msg>& friends,
                                             const std::vector<friend_request_msg>& incoming_requests,
                                             const std::vector<friend_request_msg>& outgoing_requests,
                                             const std::vector<std::string>& blocked) -> json_message;

[[nodiscard]] auto make_friend_request_notification(std::string_view requester_name) -> json_message;
[[nodiscard]] auto make_friend_accepted_notification(std::string_view friend_name) -> json_message;
[[nodiscard]] auto make_friend_online_notification(std::string_view friend_name) -> json_message;
[[nodiscard]] auto make_friend_offline_notification(std::string_view friend_name) -> json_message;

// === Guild system data structures and builders ===

struct guild_create_request_data
{
    std::string name;
    std::string tag;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<guild_create_request_data, std::string>;
};

struct guild_target_request_data
{
    std::string target_name;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<guild_target_request_data, std::string>;
};

struct guild_set_motd_request_data
{
    std::string motd;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<guild_set_motd_request_data, std::string>;
};

struct guild_member_info_msg
{
    std::string name;
    uint8_t rank{0};
    std::string rank_name;
    bool is_online{false};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

struct guild_invite_respond_request_data
{
    bool accept{false};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<guild_invite_respond_request_data, std::string>;
};

// Guild invite received notification (pushed to target, unsolicited)
[[nodiscard]] auto make_guild_invite_received(const std::string& guild_name,
                                              const std::string& guild_tag,
                                              const std::string& inviter_name) -> json_message;

// Generic guild response builder (like make_friend_response)
[[nodiscard]] auto make_guild_response(uint32_t seq,
                                       json_message_type type,
                                       bool success,
                                       std::string_view error = {},
                                       const nlohmann::json& extra = {}) -> json_message;

// Guild info response with full member list
[[nodiscard]] auto make_guild_info_response(uint32_t seq,
                                            bool success,
                                            const std::string& guild_name = {},
                                            const std::string& tag = {},
                                            const std::string& motd = {},
                                            size_t member_count = 0,
                                            const std::string& master_name = {},
                                            const std::vector<guild_member_info_msg>& members = {},
                                            std::string_view error = {}) -> json_message;

// Guild update broadcast (unsolicited, no seq)
[[nodiscard]] auto make_guild_update(const std::string& action,
                                     const std::string& guild_name,
                                     const std::string& player_name = {},
                                     const nlohmann::json& extra = {}) -> json_message;

// === Use item data structures and builders ===

struct use_item_request_data
{
    uint32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<use_item_request_data, std::string>;
};

[[nodiscard]] auto make_use_item_response(uint32_t seq,
                                          bool success,
                                          const std::string& item_name = {},
                                          const std::string& effect = {},
                                          int32_t amount = 0,
                                          int32_t current = 0,
                                          int32_t max = 0,
                                          std::string_view error = {}) -> json_message;

// === Crusade warfare builders ===

[[nodiscard]] auto make_select_duty_response(uint32_t seq,
                                             bool success,
                                             uint8_t duty = 0,
                                             int32_t construction_points = 0,
                                             std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_summon_war_unit_response(uint32_t seq,
                                                 bool success,
                                                 uint8_t unit_type = 0,
                                                 int32_t remaining_points = 0,
                                                 std::optional<std::string_view> error = std::nullopt) -> json_message;

// War reward summary builder (sent to each participant at war end)
// winner_faction: 0=neutral/draw, 1=aresden, 2=elvine
[[nodiscard]] auto make_crusade_reward_summary(uint32_t seq,
                                               uint8_t winner_faction,
                                               int32_t contribution,
                                               int64_t reward_exp,
                                               int64_t reward_gold,
                                               int32_t reward_contribution) -> json_message;

// Guild teleport response builders
[[nodiscard]] auto make_set_guild_teleport_response(
    uint32_t seq, bool success, std::optional<std::string_view> error = std::nullopt) -> json_message;

[[nodiscard]] auto make_guild_teleport_response(uint32_t seq,
                                                bool success,
                                                const std::string& map = {},
                                                int16_t x = 0,
                                                int16_t y = 0,
                                                std::optional<std::string_view> error = std::nullopt) -> json_message;

// Mana collector MP restoration broadcast
[[nodiscard]] auto
make_crusade_mp_restore(int16_t source_x, int16_t source_y, int32_t radius, int32_t your_restore) -> json_message;

// === Admin Web Tool data structures and builders ===

// Enter admin mode response builder
[[nodiscard]] auto make_enter_admin_mode_response(uint32_t seq,
                                                  bool success,
                                                  uint8_t admin_level = 0,
                                                  std::optional<std::string_view> error = std::nullopt) -> json_message;

// Admin get player request
struct admin_get_player_request_data
{
    std::string player_name; // Look up by name (preferred) or ID
    uint32_t player_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_get_player_request_data, std::string>;
};

// Admin kick player request
struct admin_kick_player_request_data
{
    std::string player_name;
    std::string reason;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_kick_player_request_data, std::string>;
};

// Admin ban player request
struct admin_ban_player_request_data
{
    std::string player_name;
    std::string reason;
    int32_t duration_hours{0}; // 0 = permanent

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_ban_player_request_data, std::string>;
};

// Admin unban player request
struct admin_unban_player_request_data
{
    std::string player_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_unban_player_request_data, std::string>;
};

// Admin teleport player request
struct admin_teleport_player_request_data
{
    std::string player_name;
    std::string dest_map;
    int16_t dest_x{0};
    int16_t dest_y{0};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_teleport_player_request_data, std::string>;
};

// Admin modify player request
struct admin_modify_player_request_data
{
    std::string player_name;
    nlohmann::json modifications; // {"hp": 100, "level": 50, "gold": 1000, ...}

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_modify_player_request_data, std::string>;
};

// Admin get map request
struct admin_get_map_request_data
{
    std::string map_name;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_get_map_request_data, std::string>;
};

// Admin spawn NPC request
struct admin_spawn_npc_request_data
{
    std::string npc_name;
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
    int16_t count{1};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_spawn_npc_request_data, std::string>;
};

// Admin kill NPC request
struct admin_kill_npc_request_data
{
    uint32_t entity_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_kill_npc_request_data, std::string>;
};

// Admin get inventory request
struct admin_get_inventory_request_data
{
    std::string player_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_get_inventory_request_data, std::string>;
};

// Admin give item request
struct admin_give_item_request_data
{
    std::string player_name;
    uint32_t item_template_id{0};
    int16_t count{1};
    std::optional<item::item_attribute> attribute; // Optional pre-set attribute

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_give_item_request_data, std::string>;
};

// Admin remove item request
struct admin_remove_item_request_data
{
    std::string player_name;
    uint32_t item_id{0};
    int16_t count{0}; // 0 = remove entire stack

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_remove_item_request_data, std::string>;
};

// Admin get guild request
struct admin_get_guild_request_data
{
    std::string guild_name;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_get_guild_request_data, std::string>;
};

// Admin get account request
struct admin_get_account_request_data
{
    std::string username;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_get_account_request_data, std::string>;
};

// Admin subscribe to map
struct admin_subscribe_map_request_data
{
    std::string map_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_subscribe_map_request_data, std::string>;
};

// Admin get map data (raw AMD binary as base64)
struct admin_get_map_data_request_data
{
    std::string map_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_get_map_data_request_data, std::string>;
};

// Admin subscribe to player (follow mode)
struct admin_subscribe_player_request_data
{
    std::string player_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_subscribe_player_request_data, std::string>;
};

// Admin response builder helpers (generic success/error pattern)
[[nodiscard]] auto make_admin_response(json_message_type type,
                                       uint32_t seq,
                                       bool success,
                                       const nlohmann::json& data = nlohmann::json::object(),
                                       std::optional<std::string_view> error = std::nullopt) -> json_message;

// Admin push notification builders
[[nodiscard]] auto
make_admin_player_connected(const std::string& name, int16_t level, const std::string& map_name) -> json_message;
[[nodiscard]] auto make_admin_player_disconnected(const std::string& name) -> json_message;
[[nodiscard]] auto
make_admin_chat_log(const std::string& channel, const std::string& sender, const std::string& content) -> json_message;

// === Admin Web Tool - Expanded request data ===

// Admin broadcast request
struct admin_broadcast_request_data
{
    std::string message;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_broadcast_request_data, std::string>;
};

// Admin mute player request
struct admin_mute_player_request_data
{
    std::string player_name;
    int32_t duration_minutes{0}; // 0 = permanent

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_mute_player_request_data, std::string>;
};

// Admin unmute player request
struct admin_unmute_player_request_data
{
    std::string player_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_unmute_player_request_data, std::string>;
};

// Admin get item template request (by id or name)
struct admin_get_item_template_request_data
{
    uint32_t item_id{0};
    std::string item_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_get_item_template_request_data, std::string>;
};

// Admin get NPC template request (by id or name)
struct admin_get_npc_template_request_data
{
    uint32_t npc_id{0};
    std::string npc_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_get_npc_template_request_data, std::string>;
};

// Admin search players request
struct admin_search_players_request_data
{
    std::string query;
    std::optional<int16_t> level_min;
    std::optional<int16_t> level_max;
    std::string map_name;
    std::optional<int> faction;
    std::string guild_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_search_players_request_data, std::string>;
};

// === Admin Web Tool - Phase 3 request data ===

// Audit log request
struct admin_get_audit_log_request_data
{
    int32_t count{100};
    std::string executor_name; // Optional filter

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_get_audit_log_request_data, std::string>;
};

// Config set request
struct admin_set_config_request_data
{
    nlohmann::json values; // Dot-notation key → value pairs

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_set_config_request_data, std::string>;
};

// Cancel scheduled task request
struct admin_cancel_scheduled_task_request_data
{
    std::string tag;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_cancel_scheduled_task_request_data, std::string>;
};

// Run canned query request
struct admin_run_query_request_data
{
    std::string query_name;
    nlohmann::json params;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_run_query_request_data, std::string>;
};

// List map NPCs request (reuses admin_get_map_request_data pattern)
struct admin_list_map_npcs_request_data
{
    std::string map_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_list_map_npcs_request_data, std::string>;
};

// List map ground items request
struct admin_list_map_ground_items_request_data
{
    std::string map_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_list_map_ground_items_request_data, std::string>;
};

// Remove ground item request
struct admin_remove_ground_item_request_data
{
    std::string map_name;
    int16_t x{0};
    int16_t y{0};
    uint32_t item_id{0};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_remove_ground_item_request_data, std::string>;
};

// Guild action request
struct admin_guild_action_request_data
{
    std::string guild_name;
    std::string action;        // "disband", "kick", "set_rank"
    std::string target_player; // For kick/set_rank
    std::string rank;          // For set_rank: "master","officer","veteran","member","recruit"

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_guild_action_request_data, std::string>;
};

// Message player request
struct admin_message_player_request_data
{
    std::string player_name;
    std::string message;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_message_player_request_data, std::string>;
};

// Set environment request
struct admin_set_environment_request_data
{
    std::string map_name; // Empty = global
    std::optional<int> weather;
    std::optional<int> hour;
    std::optional<int> minute;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_set_environment_request_data, std::string>;
};

// Shutdown server request
struct admin_shutdown_server_request_data
{
    int32_t countdown_seconds{0};
    std::string reason;
    bool cancel{false};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_shutdown_server_request_data, std::string>;
};

// === Admin Web Tool - Phase 4 request data ===

// Skill management request
struct admin_modify_skills_request_data
{
    std::string player_name;
    std::string action; // "set", "reset", "reset_all", "add_exp"
    int32_t skill_type{0};
    int32_t value{0};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_modify_skills_request_data, std::string>;
};

// Spell management request
struct admin_modify_spells_request_data
{
    std::string player_name;
    std::string action; // "learn", "forget", "level_up", "reset_cooldowns"
    uint32_t spell_id{0};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_modify_spells_request_data, std::string>;
};

// Quest inspection request
struct admin_get_player_quests_request_data
{
    std::string player_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_get_player_quests_request_data, std::string>;
};

// Quest action request
struct admin_quest_action_request_data
{
    std::string player_name;
    std::string action; // "accept", "abandon", "complete"
    uint32_t quest_id{0};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_quest_action_request_data, std::string>;
};

// Effect removal request
struct admin_remove_effects_request_data
{
    std::string player_name;
    std::string mode; // "all", "group", "single"
    int32_t group{0};
    int32_t effect_id{0};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_remove_effects_request_data, std::string>;
};

// Account creation request (admin)
struct admin_create_account_request_data
{
    std::string username;
    std::string password;
    int32_t admin_level{0};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_create_account_request_data, std::string>;
};

// Password reset request (admin)
struct admin_change_password_request_data
{
    std::string username;
    std::string new_password;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_change_password_request_data, std::string>;
};

// Admin level change request
struct admin_set_admin_level_request_data
{
    std::string username;
    int32_t admin_level{0};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_set_admin_level_request_data, std::string>;
};

// Spawn point listing request
struct admin_list_spawn_points_request_data
{
    std::string map_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_list_spawn_points_request_data, std::string>;
};

// Spell template detail request
struct admin_get_spell_template_request_data
{
    uint32_t spell_id{0};
    std::string spell_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_get_spell_template_request_data, std::string>;
};

// Maintenance mode request
struct admin_set_maintenance_mode_request_data
{
    bool enabled{false};
    std::string message;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_set_maintenance_mode_request_data, std::string>;
};

// Character creation request (admin)
struct admin_create_character_request_admin_data
{
    std::string username;
    std::string name;
    int16_t gender{1};
    int16_t hair_style{0};
    int16_t hair_color{0};
    int16_t skin_color{0};
    int16_t underwear_color{0};

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_create_character_request_admin_data, std::string>;
};

// Character deletion request (admin)
struct admin_delete_character_request_admin_data
{
    std::string username;
    std::string character_name;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_delete_character_request_admin_data, std::string>;
};

// IP ban management request
struct admin_manage_ip_bans_request_data
{
    std::string action; // "list", "add", "remove"
    std::string ip;
    std::string reason;

    [[nodiscard]] static auto
    from_json(const nlohmann::json& j) -> result<admin_manage_ip_bans_request_data, std::string>;
};

// Start task request
struct admin_start_task_request_data
{
    std::string tag;
    std::optional<int64_t> interval_ms;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_start_task_request_data, std::string>;
};

// Performance stats request
struct admin_perf_stats_request_data
{
    bool include_timing{true};
    bool include_counters{true};
    bool include_gauges{true};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_perf_stats_request_data, std::string>;
};

// === Item audit log data structures ===

struct admin_item_log_request_data
{
    std::string player_name; // optional filter
    std::string item_name;   // optional filter
    int32_t action_type{0};  // optional filter (0 = any)
    int32_t limit{50};
    int32_t offset{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<admin_item_log_request_data, std::string>;
};

auto make_admin_item_log_response(uint32_t seq,
                                  bool success,
                                  const nlohmann::json& entries,
                                  int32_t total = 0,
                                  const std::string& error = {}) -> json_message;

// === Command list data structures and builders ===

// Single command descriptor for the client
struct command_entry_msg
{
    std::string name;
    std::string description;
    std::string usage;
    std::string category;
    bool enabled{true};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

// Build full available_commands push message (sent on enter_game)
[[nodiscard]] auto make_available_commands(const std::vector<command_entry_msg>& commands) -> json_message;

// Build partial command_availability_update push message (sent on state changes)
[[nodiscard]] auto
make_command_availability_update(const std::vector<std::pair<std::string, bool>>& changes) -> json_message;

// === Combat mode messages ===

// Combat mode change response to the toggling player
[[nodiscard]] auto make_combat_mode_change_response(uint32_t seq, bool combat_mode) -> json_message;

// Combat mode change broadcast to nearby players
struct combat_mode_change_broadcast_data
{
    uint32_t entity_id{0};
    bool combat_mode{false};

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_combat_mode_change_broadcast(const combat_mode_change_broadcast_data& data) -> json_message;

// === Player action broadcast (legacy MSGID_EVENT_MOTION equivalent) ===
// Tells nearby clients what animation to play for an entity.

// Action types matching legacy motion types:
//   "attack"      (DEF_OBJECTATTACK = 3)
//   "magic"       (DEF_OBJECTMAGIC = 4)
//   "pickup"      (DEF_OBJECTGETITEM = 5)
//   "damage"      (DEF_OBJECTDAMAGE = 6)
//   "dash_attack" (DEF_OBJECTATTACKMOVE = 8)
//   "dying"       (DEF_OBJECTDYING = 10)

struct player_action_broadcast_data
{
    uint32_t entity_id{0};
    std::string action;   // Action type string (see above)
    int16_t direction{0}; // Facing direction during action

    // Optional fields — included only when relevant to the action type
    uint32_t target_id{0}; // Target entity (attack, magic, dash_attack)
    uint32_t spell_id{0};  // Spell being cast (magic)

    [[nodiscard]] auto to_json() const -> nlohmann::json;
};

[[nodiscard]] auto make_player_action_broadcast(const player_action_broadcast_data& data) -> json_message;

// === Item upgrade ===

struct item_upgrade_request_data
{
    uint32_t item_id{0}; // Item instance to upgrade

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<item_upgrade_request_data, std::string>;
};

[[nodiscard]] auto make_item_upgrade_response(
    uint32_t seq, bool success, uint32_t item_id, uint8_t new_level, std::string_view error = {}) -> json_message;

// === Special ability ===

[[nodiscard]] auto make_activate_ability_response(uint32_t seq,
                                                  bool success,
                                                  uint8_t ability_type = 0,
                                                  int32_t cooldown_sec = 0,
                                                  std::string_view error = {}) -> json_message;

// status: "disabled", "ready", "active", "cooldown"
[[nodiscard]] auto make_special_ability_status(std::string_view status,
                                               uint8_t ability_type = 0,
                                               int32_t cooldown_remaining_sec = 0) -> json_message;

// === Display name helper ===

// Format item display name with upgrade suffix: "Excalibur" → "Excalibur +7"
[[nodiscard]] inline auto get_display_name(std::string_view base_name, const item::item_attribute& attr) -> std::string
{
    if (attr.upgrade_level > 0)
    {
        return std::string(base_name) + " +" + std::to_string(attr.upgrade_level);
    }
    return std::string(base_name);
}

// === Inventory item builder ===

// Build an inventory_item_msg for a given item, looking up item and template data.
// If entry is provided, pos_x/pos_y/z_order are populated from it.
// equipped_slot is NOT set — caller must populate it from equipment_state if needed.
// Returns nullopt if the item doesn't exist.
[[nodiscard]] auto build_inventory_item_msg(
    item_id iid,
    const item::item_system* items,
    const item_registry* registry,
    const inventory::inventory_entry* entry = nullptr) -> std::optional<inventory_item_msg>;

// ============================================================
// v2 state update message builders (use item::serialize_item)
// ============================================================

// Inventory: item added
[[nodiscard]] auto make_inventory_item_add(
    const item::item& itm, int16_t pos_x, int16_t pos_y, int32_t z_order) -> nlohmann::json;

// Inventory: full item replacement
[[nodiscard]] auto make_inventory_item_update_v2(
    const item::item& itm, int16_t pos_x, int16_t pos_y, int32_t z_order) -> nlohmann::json;

// Inventory: item removed
[[nodiscard]] auto make_inventory_item_removed_v2(item_id id) -> nlohmann::json;

// Inventory: partial update (count and/or durability only)
[[nodiscard]] auto make_inventory_item_delta(
    item_id id, std::optional<int16_t> count, std::optional<int16_t> durability) -> nlohmann::json;

// Inventory: gold total changed
[[nodiscard]] auto make_inventory_gold_update(int64_t gold) -> nlohmann::json;

// Inventory: weight capacity changed
[[nodiscard]] auto make_inventory_weight_update_v2(int32_t weight, int32_t max_weight) -> nlohmann::json;

// Equipment: server forced unequip (broken, hammer_strip, armor_break)
[[nodiscard]] auto make_force_unequip(std::string_view slot, std::string_view reason) -> nlohmann::json;

// Equipment: visible equipment slot changed (broadcast to nearby)
[[nodiscard]] auto make_equipment_change(
    uint32_t entity_id, std::string_view slot, const nlohmann::json& item_json,
    int8_t appr = 0, int8_t color = 0) -> nlohmann::json;

// Ground: item appeared on map
[[nodiscard]] auto make_ground_item_spawn_v2(
    const item::item& itm, std::string_view map, int16_t x, int16_t y) -> nlohmann::json;

// Ground: item removed from map
[[nodiscard]] auto make_ground_item_removed_v2(
    item_id id, std::string_view map, int16_t x, int16_t y) -> nlohmann::json;

// Bank: slot updated with item
[[nodiscard]] auto make_bank_slot_update_v2(
    int16_t page, int16_t slot, const item::item& itm) -> nlohmann::json;

// Bank: slot cleared
[[nodiscard]] auto make_bank_slot_cleared(int16_t page, int16_t slot) -> nlohmann::json;

// Ability: activated on entity
[[nodiscard]] auto make_ability_activated(
    uint32_t entity_id, std::string_view ability_type, int32_t duration_ms) -> nlohmann::json;

// Ability: expired on entity
[[nodiscard]] auto make_ability_expired(
    uint32_t entity_id, std::string_view ability_type) -> nlohmann::json;

// Inventory: full inventory snapshot sent on login
// items: each tuple is (serialized_item_json, pos_x, pos_y, z_order)
// equipment_slots: slot_name -> item_id for occupied slots
[[nodiscard]] auto make_inventory_data_v2(
    const std::vector<std::tuple<nlohmann::json, int16_t, int16_t, int32_t>>& items,
    const std::map<std::string, uint32_t>& equipment_slots,
    int64_t gold,
    int32_t weight,
    int32_t max_weight) -> nlohmann::json;

// ============================================================
// v2 action message data structs and builders
// ============================================================

// --- Inventory reposition (no response) ---

struct inventory_reposition_data
{
    int32_t item_id{0};
    int32_t pos_x{0};
    int32_t pos_y{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<inventory_reposition_data, std::string>;
};

// --- Equip ---

struct equip_request_data
{
    int32_t item_id{0};
    std::string slot;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<equip_request_data, std::string>;
};

[[nodiscard]] auto make_equip_result(bool success, std::string_view slot) -> nlohmann::json;

// --- Unequip ---

struct unequip_request_data
{
    std::string slot;
    std::optional<int16_t> pos_x;
    std::optional<int16_t> pos_y;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<unequip_request_data, std::string>;
};

[[nodiscard]] auto make_unequip_result(bool success, std::string_view slot) -> nlohmann::json;

// --- Pickup ---

struct pickup_request_data
{
    std::string map;
    int32_t x{0};
    int32_t y{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<pickup_request_data, std::string>;
};

[[nodiscard]] auto make_pickup_result(bool success) -> nlohmann::json;

// --- Drop ---

struct drop_request_data
{
    int32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<drop_request_data, std::string>;
};

[[nodiscard]] auto make_drop_result(bool success) -> nlohmann::json;

// --- Use item ---

struct use_item_request_data_v2
{
    int32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<use_item_request_data_v2, std::string>;
};

[[nodiscard]] auto make_use_item_result(bool success) -> nlohmann::json;

// --- Upgrade ---

struct upgrade_request_data
{
    int32_t target_id{0};
    int32_t stone_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<upgrade_request_data, std::string>;
};

[[nodiscard]] auto make_upgrade_result_v2(bool success) -> nlohmann::json;

// --- Shop buy ---

struct shop_buy_request_data_v2
{
    int32_t template_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_buy_request_data_v2, std::string>;
};

[[nodiscard]] auto make_shop_buy_result(bool success) -> nlohmann::json;

// --- Shop sell ---

struct shop_sell_request_data_v2
{
    int32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_sell_request_data_v2, std::string>;
};

[[nodiscard]] auto make_shop_sell_result(bool success) -> nlohmann::json;

// --- Shop repair ---

struct shop_repair_request_data_v2
{
    int32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<shop_repair_request_data_v2, std::string>;
};

[[nodiscard]] auto make_shop_repair_result(bool success) -> nlohmann::json;

// --- Bank deposit ---

struct bank_deposit_request_data_v2
{
    int32_t item_id{0};
    std::optional<int32_t> page;
    std::optional<int32_t> slot;

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<bank_deposit_request_data_v2, std::string>;
};

[[nodiscard]] auto make_bank_deposit_result(bool success) -> nlohmann::json;

// --- Bank withdraw ---

struct bank_withdraw_request_data_v2
{
    int32_t page{0};
    int32_t slot{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<bank_withdraw_request_data_v2, std::string>;
};

[[nodiscard]] auto make_bank_withdraw_result(bool success) -> nlohmann::json;

// --- Bank reposition ---

struct bank_reposition_request_data
{
    int32_t from_page{0};
    int32_t from_slot{0};
    int32_t to_page{0};
    int32_t to_slot{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<bank_reposition_request_data, std::string>;
};

[[nodiscard]] auto make_bank_reposition_result(bool success) -> nlohmann::json;

// --- Activate ability ---

struct activate_ability_request_data
{
    int32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<activate_ability_request_data, std::string>;
};

[[nodiscard]] auto make_activate_ability_failed(std::string_view error) -> nlohmann::json;

// ============================================================
// v2 trade message data structs and builders
// ============================================================

// --- Phase 0: Initiating ---

struct trade_request_data_v2
{
    int32_t target_entity_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<trade_request_data_v2, std::string>;
};

[[nodiscard]] auto make_trade_invite(uint32_t from_entity_id, std::string_view from_name) -> nlohmann::json;

struct trade_accept_data
{
    int32_t from_entity_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<trade_accept_data, std::string>;
};

struct trade_decline_data
{
    int32_t from_entity_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<trade_decline_data, std::string>;
};

[[nodiscard]] auto make_trade_opened(uint32_t partner_entity_id, std::string_view partner_name) -> nlohmann::json;

// --- Phase 1: Offer ---

struct trade_add_item_data
{
    int32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<trade_add_item_data, std::string>;
};

struct trade_remove_item_data
{
    int32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<trade_remove_item_data, std::string>;
};

struct trade_set_gold_data
{
    int64_t amount{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<trade_set_gold_data, std::string>;
};

[[nodiscard]] auto make_trade_update(
    std::string_view side, const std::vector<nlohmann::json>& items, int64_t gold) -> nlohmann::json;

// --- Phase 2: Lock ---

// trade_lock: empty data, no struct needed

[[nodiscard]] auto make_trade_lock_status(bool my_locked, bool their_locked) -> nlohmann::json;

// --- Phase 3: Confirm ---

// trade_confirm: empty data, no struct needed

[[nodiscard]] auto make_trade_complete(bool success) -> nlohmann::json;

// --- Cancellation ---

// trade_cancel: empty data, no struct needed

[[nodiscard]] auto make_trade_canceled(std::string_view reason) -> nlohmann::json;

// ============================================================
// Shop and bank open messages (v2, serialize_item-based)
// ============================================================

// Shop: catalog of items available for purchase
// items: each pair is (serialized_item_json, buy_price)
[[nodiscard]] auto make_shop_open(
    std::string_view npc_name,
    std::string_view shop_type,
    const std::vector<std::pair<nlohmann::json, int32_t>>& items) -> nlohmann::json;

// Bank: full bank contents sent when opening bank
// pages: outer = pages, inner = slots (null JSON for empty)
[[nodiscard]] auto make_bank_open_v2(
    const std::vector<std::vector<nlohmann::json>>& pages,
    int16_t total_pages) -> nlohmann::json;

// ============================================================
// Party loot distribution messages
// ============================================================

// --- Client -> Server (parsers) ---

struct set_loot_rule_data
{
    std::string rule; // "disabled", "greed", or "master"

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<set_loot_rule_data, std::string>;
};

struct loot_roll_data
{
    std::string loot_id;
    int32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<loot_roll_data, std::string>;
};

struct loot_pass_data
{
    std::string loot_id;
    int32_t item_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<loot_pass_data, std::string>;
};

struct loot_assign_data
{
    std::string loot_id;
    int32_t item_id{0};
    int32_t target_entity_id{0};

    [[nodiscard]] static auto from_json(const nlohmann::json& j) -> result<loot_assign_data, std::string>;
};

// --- Server -> Client (builders) ---

[[nodiscard]] auto make_loot_rule_changed(std::string_view rule, uint32_t set_by) -> nlohmann::json;

[[nodiscard]] auto make_loot_available(
    std::string_view loot_id,
    const std::vector<nlohmann::json>& items,
    std::string_view source_map,
    int16_t source_x,
    int16_t source_y,
    std::string_view rule,
    int32_t timeout_ms) -> nlohmann::json;

[[nodiscard]] auto make_loot_roll_result(
    std::string_view loot_id,
    uint32_t item_id,
    uint32_t entity_id,
    std::string_view player_name,
    int32_t roll) -> nlohmann::json;

[[nodiscard]] auto make_loot_awarded(
    std::string_view loot_id,
    uint32_t item_id,
    uint32_t winner_entity_id,
    std::string_view winner_name) -> nlohmann::json;

[[nodiscard]] auto make_loot_expired(std::string_view loot_id) -> nlohmann::json;

} // namespace hb::network
