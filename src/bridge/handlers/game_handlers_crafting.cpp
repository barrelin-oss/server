#include "platform/platform.h"
#include "bridge/handlers/game_handlers.h"
#include "bridge/handlers/broadcast_util.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "crafting/manufacturing_system.h"
#include "crafting/alchemy_system.h"
#include "crafting/mining_system.h"
#include "crafting/fishing_system.h"
#include "registry/build_recipe_registry.h"
#include "registry/craft_recipe_registry.h"
#include "skill/skill_system.h"
#include "quest/quest_system.h"
#include "war/crusade/crusade_system.h"
#include "audit/item_audit_system.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include "perf/perf_stats.h"

namespace hb::bridge
{

void game_handlers::handle_manufacture_list_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!manufacturing_)
    {
        send_error(conn_id, msg.seq, "not_available", "Manufacturing is not available");
        return;
    }

    auto pid = conn->player();
    auto recipes = manufacturing_->get_available_recipes(entity_id{pid.value});

    nlohmann::json recipe_list = nlohmann::json::array();
    for (const auto* recipe : recipes)
    {
        nlohmann::json r;
        r["id"] = recipe->id;
        r["name"] = recipe->result;
        r["skill_req"] = recipe->skill_req;
        r["success_rate"] = recipe->success_rate;

        nlohmann::json ings = nlohmann::json::array();
        for (const auto& ing : recipe->ingredients)
        {
            ings.push_back({{"item_id", ing.item_id}, {"count", ing.count}});
        }
        r["ingredients"] = ings;

        recipe_list.push_back(r);
    }

    conn->send(network::make_manufacture_list_response(msg.seq, recipe_list));
}

void game_handlers::handle_manufacture_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!manufacturing_)
    {
        send_error(conn_id, msg.seq, "not_available", "Manufacturing is not available");
        return;
    }

    auto parse = network::manufacture_request_data::from_json(msg.data);
    if (parse.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_data", parse.error());
        return;
    }

    auto& data = parse.value();
    auto pid = conn->player();

    // Snapshot inventory state before crafting (to detect consumed/created slots)
    auto* inv = inventory_->get_inventory(entity_id{pid.value});
    struct slot_snapshot { item_id item{}; int16_t count{}; };
    std::vector<slot_snapshot> slots_before;
    if (inv)
    {
        slots_before.resize(static_cast<size_t>(inv->capacity()));
        for (int16_t i = 0; i < inv->capacity(); ++i)
        {
            auto* s = inv->get_slot(i);
            if (s && !s->is_empty())
            {
                slots_before[static_cast<size_t>(i)] = {s->item, s->count};
            }
        }
    }

    // Helper to diff inventory and send slot updates for any changes
    auto send_slot_updates = [&]()
    {
        if (!inv)
            return;
        for (int16_t i = 0; i < inv->capacity(); ++i)
        {
            auto* s = inv->get_slot(i);
            bool was_occupied = slots_before[static_cast<size_t>(i)].item.is_valid();
            bool is_occupied = s && !s->is_empty();

            if (was_occupied && !is_occupied)
            {
                // Slot was cleared (consumed material)
                conn->send(network::make_inventory_slot_update(i, nullptr));
            }
            else if (!was_occupied && is_occupied)
            {
                // Slot was filled (crafted result)
                auto item_msg = network::build_inventory_item_msg(i, s->item, item_, item_registry_);
                if (item_msg)
                    conn->send(network::make_inventory_slot_update(i, &*item_msg));
            }
            else if (was_occupied && is_occupied
                     && slots_before[static_cast<size_t>(i)].count != s->count)
            {
                // Count changed (partial stack consumed)
                auto item_msg = network::build_inventory_item_msg(i, s->item, item_, item_registry_);
                if (item_msg)
                    conn->send(network::make_inventory_slot_update(i, &*item_msg));
            }
        }
    };

    auto result = manufacturing_->attempt_craft(entity_id{pid.value}, data.recipe_index);

    if (result.reason == skill::skill_use_result::insufficient_skill)
    {
        conn->send(network::make_manufacture_response(msg.seq, false, "", "insufficient_skill"));
        send_slot_updates();
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_materials)
    {
        conn->send(network::make_manufacture_response(msg.seq, false, "", "insufficient_materials"));
        send_slot_updates();
        return;
    }
    if (!result.success && result.reason == skill::skill_use_result::failure)
    {
        conn->send(network::make_manufacture_response(msg.seq, false, "", "inventory_full"));
        send_slot_updates();
        return;
    }

    // Look up recipe name for response
    std::string item_name;
    auto* registry = subsystems().get<build_recipe_registry>();
    if (registry)
    {
        auto* recipe = registry->get(data.recipe_index);
        if (recipe)
            item_name = recipe->result;
    }

    conn->send(network::make_manufacture_response(msg.seq, result.success, item_name));
    send_slot_updates();

    // Audit crafted item
    if (result.success && audit_ && result.created_item.is_valid())
    {
        if (auto* plr = players_->get_player(pid))
        {
            if (auto* crafted = item_ ? item_->get_item(result.created_item) : nullptr)
            {
                if (crafted->audited)
                {
                    audit_->log_item(static_cast<int32_t>(plr->character_id.value),
                                     item_name.empty() ? crafted->name : item_name,
                                     static_cast<int32_t>(result.created_item.value),
                                     item_log_type::make,
                                     1);
                }
            }
        }
    }

    // Fire quest event on success
    if (result.success && quests_ && result.created_item.is_valid())
    {
        quests_->on_item_crafted({.player = pid, .item = result.created_item, .count = 1});
    }
}

// === Crafting: Alchemy handlers ===

void game_handlers::handle_alchemy_list_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!alchemy_)
    {
        send_error(conn_id, msg.seq, "not_available", "Alchemy is not available");
        return;
    }

    auto pid = conn->player();
    auto recipes = alchemy_->get_available_recipes(entity_id{pid.value});

    nlohmann::json recipe_list = nlohmann::json::array();
    for (const auto* recipe : recipes)
    {
        nlohmann::json r;
        r["id"] = recipe->id;
        r["name"] = recipe->result;
        r["skill_limit"] = recipe->skill_limit;
        r["difficulty"] = recipe->difficulty;

        nlohmann::json ings = nlohmann::json::array();
        for (const auto& ing : recipe->ingredients)
        {
            ings.push_back({{"item_id", ing.item_id}, {"count", ing.count}});
        }
        r["ingredients"] = ings;

        recipe_list.push_back(r);
    }

    conn->send(network::make_alchemy_list_response(msg.seq, recipe_list));
}

void game_handlers::handle_alchemy_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!alchemy_)
    {
        send_error(conn_id, msg.seq, "not_available", "Alchemy is not available");
        return;
    }

    auto parse = network::alchemy_request_data::from_json(msg.data);
    if (parse.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_data", parse.error());
        return;
    }

    auto& data = parse.value();
    auto pid = conn->player();

    // Snapshot inventory state before crafting (to detect consumed/created slots)
    auto* inv = inventory_->get_inventory(entity_id{pid.value});
    struct slot_snapshot { item_id item{}; int16_t count{}; };
    std::vector<slot_snapshot> slots_before;
    if (inv)
    {
        slots_before.resize(static_cast<size_t>(inv->capacity()));
        for (int16_t i = 0; i < inv->capacity(); ++i)
        {
            auto* s = inv->get_slot(i);
            if (s && !s->is_empty())
            {
                slots_before[static_cast<size_t>(i)] = {s->item, s->count};
            }
        }
    }

    // Helper to diff inventory and send slot updates for any changes
    auto send_slot_updates = [&]()
    {
        if (!inv)
            return;
        for (int16_t i = 0; i < inv->capacity(); ++i)
        {
            auto* s = inv->get_slot(i);
            bool was_occupied = slots_before[static_cast<size_t>(i)].item.is_valid();
            bool is_occupied = s && !s->is_empty();

            if (was_occupied && !is_occupied)
            {
                // Slot was cleared (consumed material)
                conn->send(network::make_inventory_slot_update(i, nullptr));
            }
            else if (!was_occupied && is_occupied)
            {
                // Slot was filled (crafted result)
                auto item_msg = network::build_inventory_item_msg(i, s->item, item_, item_registry_);
                if (item_msg)
                    conn->send(network::make_inventory_slot_update(i, &*item_msg));
            }
            else if (was_occupied && is_occupied
                     && slots_before[static_cast<size_t>(i)].count != s->count)
            {
                // Count changed (partial stack consumed)
                auto item_msg = network::build_inventory_item_msg(i, s->item, item_, item_registry_);
                if (item_msg)
                    conn->send(network::make_inventory_slot_update(i, &*item_msg));
            }
        }
    };

    auto result = alchemy_->attempt_craft(entity_id{pid.value}, data.recipe_id);

    if (result.reason == skill::skill_use_result::insufficient_skill)
    {
        conn->send(network::make_alchemy_response(msg.seq, false, "", "insufficient_skill"));
        send_slot_updates();
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_materials)
    {
        conn->send(network::make_alchemy_response(msg.seq, false, "", "insufficient_materials"));
        send_slot_updates();
        return;
    }
    if (!result.success && result.reason == skill::skill_use_result::failure)
    {
        conn->send(network::make_alchemy_response(msg.seq, false, "", "inventory_full"));
        send_slot_updates();
        return;
    }

    // Look up recipe name for response
    std::string item_name;
    auto* registry = subsystems().get<craft_recipe_registry>();
    if (registry)
    {
        auto* recipe = registry->get(data.recipe_id);
        if (recipe)
            item_name = recipe->result;
    }

    conn->send(network::make_alchemy_response(msg.seq, result.success, item_name));
    send_slot_updates();

    // Audit crafted item
    if (result.success && audit_ && result.created_item.is_valid())
    {
        if (auto* plr = players_->get_player(pid))
        {
            if (auto* crafted = item_ ? item_->get_item(result.created_item) : nullptr)
            {
                if (crafted->audited)
                {
                    audit_->log_item(static_cast<int32_t>(plr->character_id.value),
                                     item_name.empty() ? crafted->name : item_name,
                                     static_cast<int32_t>(result.created_item.value),
                                     item_log_type::make,
                                     1);
                }
            }
        }
    }

    // Fire quest event on success
    if (result.success && quests_ && result.created_item.is_valid())
    {
        quests_->on_item_crafted({.player = pid, .item = result.created_item, .count = 1});
    }
}

// === Mining ===

void game_handlers::handle_mine_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!mining_)
    {
        send_error(conn_id, msg.seq, "not_available", "Mining is not available");
        return;
    }

    auto parse = network::mine_request_data::from_json(msg.data);
    if (parse.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_data", parse.error());
        return;
    }

    auto& data = parse.value();
    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    auto* map = world_->get_map(plr->current_map);
    if (!map)
    {
        send_error(conn_id, msg.seq, "internal_error", "Map not found");
        return;
    }
    auto map_name = std::string(map->name());
    auto result = mining_->attempt_mine(entity_id(pid.value), data.target_x, data.target_y, map_name);

    if (result.reason == skill::skill_use_result::invalid_target)
    {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, false, "invalid_target"));
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_materials)
    {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, false, "no_pickaxe"));
        return;
    }
    if (result.reason == skill::skill_use_result::insufficient_skill)
    {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, false, "insufficient_skill"));
        return;
    }

    if (!result.success)
    {
        conn->send(network::make_mine_response(msg.seq, false, "", 0, result.node_depleted, "miss"));
        return;
    }

    conn->send(network::make_mine_response(msg.seq, true, result.item_name, result.template_id, result.node_depleted));
}

// === Fishing ===

void game_handlers::handle_fish_skill_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!fishing_)
    {
        send_error(conn_id, msg.seq, "not_available", "Fishing is not available");
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    // Check if player has fishing skill
    if (skills_)
    {
        auto skill_level = skills_->get_skill_level(pid, skill::skill_type::fishing);
        if (skill_level == 0)
        {
            conn->send(network::make_fish_skill_response(msg.seq, false, "You don't know how to fish"));
            return;
        }
    }

    // Check if already fishing
    if (plr->fishing.fish_node_index != 0)
    {
        conn->send(network::make_fish_skill_response(msg.seq, false, "Already fishing"));
        return;
    }

    auto* map = world_->get_map(plr->current_map);
    if (!map)
    {
        send_error(conn_id, msg.seq, "internal_error", "Map not found");
        return;
    }

    auto map_name = std::string(map->name());
    auto fish_index = fishing_->check_fish_nearby(entity_id(pid.value), map_name, plr->pos.x, plr->pos.y);

    if (!fish_index)
    {
        conn->send(network::make_fish_skill_response(msg.seq, false, "No fish nearby"));
        return;
    }

    // Success — engaged callback already fired by fishing_system
    conn->send(network::make_fish_skill_response(msg.seq, true));
}

void game_handlers::handle_fish_catch_request(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!fishing_)
    {
        send_error(conn_id, msg.seq, "not_available", "Fishing is not available");
        return;
    }

    auto pid = conn->player();
    auto* plr = players_->get_player(pid);
    if (!plr)
    {
        send_error(conn_id, msg.seq, "internal_error", "Player not found");
        return;
    }

    // attempt_catch handles all validation and fires callbacks
    fishing_->attempt_catch(entity_id(pid.value));
    // Response sent via catch_complete_callback
}

// ========== Crusade Handlers ==========

void game_handlers::handle_select_duty(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!crusade_ || !crusade_->is_active())
    {
        conn->send(network::make_select_duty_response(msg.seq, false, 0, 0, "no_active_crusade"));
        return;
    }

    auto duty_val = msg.data.value("duty", 0);
    if (duty_val < 1 || duty_val > 3)
    {
        conn->send(network::make_select_duty_response(msg.seq, false, 0, 0, "invalid_duty"));
        return;
    }

    auto duty = static_cast<war::crusade_duty>(duty_val);
    auto pid = conn->player();

    // Auto-join the crusade if not already in it
    if (!crusade_->is_in_crusade(pid))
    {
        if (auto* plr = players_->get_player(pid))
        {
            auto faction = war::war_faction::neutral;
            if (plr->faction == hb::faction::aresden)
                faction = war::war_faction::aresden;
            else if (plr->faction == hb::faction::elvine)
                faction = war::war_faction::elvine;

            auto join_result = crusade_->join_crusade(pid, faction);
            if (join_result != war::crusade_result::success)
            {
                conn->send(network::make_select_duty_response(msg.seq, false, 0, 0, "cannot_join"));
                return;
            }
        }
    }

    auto result = crusade_->select_duty(pid, duty);
    if (result != war::crusade_result::success)
    {
        std::string_view err = "unknown_error";
        switch (result)
        {
        case war::crusade_result::already_has_duty:
            err = "already_has_duty";
            break;
        case war::crusade_result::not_in_crusade:
            err = "not_in_crusade";
            break;
        default:
            break;
        }
        conn->send(network::make_select_duty_response(msg.seq, false, 0, 0, err));
        return;
    }

    auto* data = crusade_->get_player_data(pid);
    conn->send(network::make_select_duty_response(
        msg.seq, true, static_cast<uint8_t>(duty), data ? data->construction_points : 0));
}

void game_handlers::handle_summon_war_unit(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!crusade_ || !crusade_->is_active())
    {
        conn->send(network::make_summon_war_unit_response(msg.seq, false, 0, 0, "no_active_crusade"));
        return;
    }

    auto unit_val = msg.data.value("unit_type", 0);
    if (unit_val < 1 || unit_val > 11)
    {
        conn->send(network::make_summon_war_unit_response(msg.seq, false, 0, 0, "invalid_unit_type"));
        return;
    }

    auto unit_type = static_cast<war::war_unit_type>(unit_val);
    auto pid = conn->player();

    auto map_name = msg.data.value("map", std::string{});
    auto x = msg.data.value("x", int16_t{0});
    auto y = msg.data.value("y", int16_t{0});

    auto result = crusade_->summon_war_unit(pid, unit_type, map_name, x, y);
    if (result != war::crusade_result::success)
    {
        std::string_view err = "unknown_error";
        switch (result)
        {
        case war::crusade_result::not_constructor:
            err = "not_constructor";
            break;
        case war::crusade_result::insufficient_points:
            err = "insufficient_points";
            break;
        case war::crusade_result::not_in_crusade:
            err = "not_in_crusade";
            break;
        case war::crusade_result::invalid_unit:
            err = "invalid_unit";
            break;
        case war::crusade_result::restricted_map:
            err = "restricted_map";
            break;
        case war::crusade_result::no_construct_location:
            err = "no_construct_location";
            break;
        case war::crusade_result::too_far_from_construct_location:
            err = "too_far_from_construct_location";
            break;
        case war::crusade_result::guild_build_limit:
            err = "guild_build_limit";
            break;
        case war::crusade_result::too_close_to_tower:
            err = "too_close_to_tower";
            break;
        case war::crusade_result::invalid_position:
            err = "invalid_position";
            break;
        case war::crusade_result::wrong_map:
            err = "wrong_map";
            break;
        default:
            break;
        }
        conn->send(network::make_summon_war_unit_response(msg.seq, false, 0, 0, err));
        return;
    }

    auto* data = crusade_->get_player_data(pid);
    conn->send(network::make_summon_war_unit_response(
        msg.seq, true, static_cast<uint8_t>(unit_type), data ? data->construction_points : 0));
}

void game_handlers::handle_crusade_map_status(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!crusade_ || !crusade_->is_active())
        return;

    auto pid = conn->player();
    auto* data = crusade_->get_player_data(pid);
    if (!data || data->duty != war::crusade_duty::commander)
        return;

    // Build structure list
    nlohmann::json structures = nlohmann::json::array();
    for (const auto& ws : crusade_->get_war_structures())
    {
        structures.push_back({{"type", static_cast<int>(ws.type)},
                              {"faction", static_cast<int>(ws.faction)},
                              {"map", ws.map_name},
                              {"x", ws.x},
                              {"y", ws.y}});
    }

    network::json_message response;
    response.type = network::json_message_type::crusade_map_status;
    response.seq = msg.seq;
    response.data = {{"structures", std::move(structures)}};

    conn->send(response);
}

void game_handlers::handle_set_guild_teleport(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!crusade_ || !crusade_->is_active())
    {
        conn->send(network::make_set_guild_teleport_response(msg.seq, false, "no_active_crusade"));
        return;
    }

    auto map_name = msg.data.value("map", std::string{});
    auto x = msg.data.value("x", int16_t{0});
    auto y = msg.data.value("y", int16_t{0});

    auto result = crusade_->set_guild_teleport_location(conn->player(), map_name, x, y);
    if (result != war::crusade_result::success)
    {
        std::string_view err = "unknown_error";
        switch (result)
        {
        case war::crusade_result::not_in_crusade:
            err = "not_in_crusade";
            break;
        case war::crusade_result::not_guild_master:
            err = "not_guild_master";
            break;
        case war::crusade_result::invalid_position:
            err = "invalid_position";
            break;
        default:
            break;
        }
        conn->send(network::make_set_guild_teleport_response(msg.seq, false, err));
        return;
    }

    conn->send(network::make_set_guild_teleport_response(msg.seq, true));
}

void game_handlers::handle_guild_teleport(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;

    if (!crusade_ || !crusade_->is_active())
    {
        conn->send(network::make_guild_teleport_response(msg.seq, false, {}, 0, 0, "no_active_crusade"));
        return;
    }

    auto pid = conn->player();
    auto result = crusade_->use_guild_teleport(pid);
    if (result != war::crusade_result::success)
    {
        std::string_view err = "unknown_error";
        switch (result)
        {
        case war::crusade_result::not_in_crusade:
            err = "not_in_crusade";
            break;
        case war::crusade_result::no_construct_location:
            err = "no_teleport_set";
            break;
        default:
            break;
        }
        conn->send(network::make_guild_teleport_response(msg.seq, false, {}, 0, 0, err));
        return;
    }

    auto* dest = crusade_->get_guild_teleport_dest(pid);
    if (!dest)
    {
        conn->send(network::make_guild_teleport_response(msg.seq, false, {}, 0, 0, "no_teleport_set"));
        return;
    }

    conn->send(network::make_guild_teleport_response(msg.seq, true, dest->map_name, dest->x, dest->y));

    // Execute the teleport
    execute_player_teleport(pid, conn_id, msg.seq, dest->map_name, {dest->x, dest->y}, world::direction::south);
}

} // namespace hb::bridge
