// game_handlers_quest.cpp
// Player-facing quest flow over the JSON protocol: list what a city hall officer
// offers, accept, abandon, turn in, read the journal, and push progress after kills.
// The quest_system owns the rules; this file only maps them to the wire and applies
// rewards (XP, gold, items) through the player/inventory systems.
#include "platform/platform.h"
#include "bridge/handlers/game_handlers.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "registry/item_registry.h"
#include "registry/npc_registry.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "quest/quest_system.h"
#include "core/subsystem.h"
#include "core/logger.h"

#include <algorithm>

namespace hb::bridge
{

namespace
{

auto status_name(const quest::quest_state* st) -> const char*
{
    if (!st)
        return "available";
    switch (st->status)
    {
    case quest::quest_status::active:
        return "active";
    case quest::quest_status::complete:
        return "complete";
    case quest::quest_status::turned_in:
        return "turned_in";
    case quest::quest_status::failed:
        return "failed";
    case quest::quest_status::abandoned:
        return "abandoned";
    case quest::quest_status::available:
    default:
        return "available";
    }
}

auto objective_type_name(quest::objective_type t) -> const char*
{
    switch (t)
    {
    case quest::objective_type::kill_monster:
        return "kill";
    case quest::objective_type::kill_player:
        return "kill_player";
    case quest::objective_type::collect_item:
        return "collect";
    case quest::objective_type::deliver_item:
        return "deliver";
    case quest::objective_type::visit_location:
        return "visit";
    case quest::objective_type::talk_to_npc:
        return "talk";
    default:
        return "other";
    }
}

auto rewards_to_json(const quest::quest_rewards& r) -> nlohmann::json
{
    nlohmann::json items = nlohmann::json::array();
    for (const auto& it : r.items)
        items.push_back({{"item_id", it.item_type.value}, {"count", it.count}});
    return {{"experience", r.experience}, {"gold", r.gold}, {"items", std::move(items)}};
}

} // namespace

auto game_handlers::quest_to_json(const quest::quest_template& t, const quest::quest_state* st) const -> nlohmann::json
{
    const auto* npc_reg = subsystems().get<npc_registry>();
    nlohmann::json objectives = nlohmann::json::array();
    for (size_t i = 0; i < t.objectives.size(); ++i)
    {
        const auto& ot = t.objectives[i];
        nlohmann::json o{{"description", ot.description}, {"type", objective_type_name(ot.type)}};
        if (const auto* kill = std::get_if<quest::kill_objective_data>(&ot.data))
        {
            o["target_npc_id"] = kill->target_type.value;
            if (npc_reg)
                if (const auto* tmpl = npc_reg->get(kill->target_type))
                    o["target_name"] = tmpl->name;
            o["required"] = kill->required_count;
        }
        else if (const auto* loc = std::get_if<quest::location_objective_data>(&ot.data))
        {
            o["map_id"] = loc->target_map.value;
            o["x"] = loc->target_x;
            o["y"] = loc->target_y;
            o["radius"] = loc->radius;
            o["required"] = 1;
        }
        if (st && i < st->objectives.size())
        {
            o["current"] = st->objectives[i].current_count;
            o["required"] = st->objectives[i].required_count;
            o["complete"] = st->objectives[i].is_complete();
        }
        else
        {
            o["current"] = 0;
            o["complete"] = false;
        }
        objectives.push_back(std::move(o));
    }

    return {{"quest_id", t.id.value},
            {"name", t.name},
            {"description", t.description},
            {"min_level", t.min_level},
            {"max_level", t.max_level},
            {"repeatable", t.repeatable},
            {"giver_npc_id", t.quest_giver.value},
            {"status", status_name(st)},
            {"objectives", std::move(objectives)},
            {"rewards", rewards_to_json(t.rewards)}};
}

// Everything a given officer has to say to this player: quests they could accept
// now, plus the ones already taken from that officer with their progress.
auto game_handlers::quests_offered_by(const player::player& plr, npc_id giver) -> nlohmann::json
{
    nlohmann::json out = nlohmann::json::array();
    if (!quests_)
        return out;

    const auto* journal = quests_->get_journal(plr.id);
    if (journal)
    {
        for (const auto& st : journal->active_quests)
        {
            if (!st.is_active() && !st.is_complete())
                continue;
            const auto* t = quests_->get_quest_template(st.template_id);
            if (t && t->quest_giver == giver)
                out.push_back(quest_to_json(*t, &st));
        }
    }

    const auto level = static_cast<int16_t>(plr.experience.level);
    for (auto id : quests_->get_available_quests(plr.id, level, static_cast<uint8_t>(plr.faction)))
    {
        const auto* t = quests_->get_quest_template(id);
        if (!t || t->quest_giver != giver)
            continue;
        if (journal && journal->has_active_quest(id))
            continue;
        out.push_back(quest_to_json(*t, nullptr));
    }
    return out;
}

void game_handlers::send_quest_update(const player::player& plr,
                                      const quest::quest_template& t,
                                      const quest::quest_state& st)
{
    auto* conn = ws_server_ ? ws_server_->get_connection(plr.connection) : nullptr;
    if (conn)
        conn->send(network::make_quest_update(quest_to_json(t, &st)));
}

// Called from the NPC death callback. Feeds the kill to the quest system and pushes
// progress for every quest of the killer that tracks this NPC type.
void game_handlers::notify_quest_kill(const npc::npc& n, entity::entity killer)
{
    if (!quests_ || !players_)
        return;
    auto pid_opt = players_->get_player_id_by_entity(killer);
    if (!pid_opt)
        return;
    const auto pid = *pid_opt;
    auto* plr = players_->get_player(pid);
    if (!plr)
        return;

    quests_->on_kill(quest::kill_event{.killer = pid, .killed_npc = n.template_id, .was_player = false});

    const auto* journal = quests_->get_journal(pid);
    if (!journal)
        return;
    for (const auto& st : journal->active_quests)
    {
        if (!st.is_active() && !st.is_complete())
            continue;
        const auto* t = quests_->get_quest_template(st.template_id);
        if (!t)
            continue;
        const bool tracks = std::any_of(t->objectives.begin(),
                                        t->objectives.end(),
                                        [&](const quest::objective_template& ot)
                                        {
                                            const auto* kill = std::get_if<quest::kill_objective_data>(&ot.data);
                                            return kill && !kill->player_kills &&
                                                   (kill->target_type.value == 0 || kill->target_type == n.template_id);
                                        });
        if (tracks)
            send_quest_update(*plr, *t, st);
    }
}

// quest_system fires this from complete_quest(); rewards are applied here because
// only the bridge has the player, inventory and item systems at hand.
void game_handlers::apply_quest_rewards(const quest::quest_completed_event& ev)
{
    if (!players_)
        return;
    auto* plr = players_->get_player(ev.player);
    if (!plr)
        return;
    const auto owner = entity_id(ev.player.value);

    if (ev.rewards.experience > 0)
        players_->add_experience(ev.player, ev.rewards.experience);

    if (ev.rewards.gold > 0 && inventory_)
    {
        inventory_->add_gold(owner, ev.rewards.gold);
        if (auto* conn = ws_server_ ? ws_server_->get_connection(plr->connection) : nullptr)
        {
            conn->send(network::make_gold_update({.gold = static_cast<int64_t>(inventory_->get_gold(owner)),
                                                  .change = static_cast<int64_t>(ev.rewards.gold),
                                                  .reason = "quest_reward"}));
        }
    }

    for (const auto& it : ev.rewards.items)
    {
        if (!item_ || !inventory_)
            break;
        if (inventory_->is_full(owner))
        {
            LOG_WARN(bridge,
                     "Quest {} reward item {} x{} dropped for player {}: inventory full",
                     ev.quest.value,
                     it.item_type.value,
                     it.count,
                     ev.player.value);
            continue;
        }
        auto created = item_->create_from_template(it.item_type, it.count);
        if (created.is_err())
        {
            LOG_WARN(bridge, "Quest {} reward item {} could not be created: {}", ev.quest.value, it.item_type.value, created.error());
            continue;
        }
        item_->set_owner(created.value(), owner);
        if (inventory_->add_item(owner, created.value(), it.count) != inventory::inventory_result::success)
        {
            item_->destroy_item(created.value());
            LOG_WARN(bridge, "Quest {} reward item {} could not be added to inventory", ev.quest.value, it.item_type.value);
        }
    }

    LOG_INFO(bridge,
             "Player {} turned in quest {}: +{} XP, +{} gold, {} item(s)",
             plr->name,
             ev.quest.value,
             ev.rewards.experience,
             ev.rewards.gold,
             ev.rewards.items.size());
}

// Turns in every quest from this officer whose objectives are done. Returns how many.
auto game_handlers::complete_quests_at_npc(const player::player& plr, npc_id giver, nlohmann::json& completed) -> int
{
    if (!quests_)
        return 0;
    const auto* journal = quests_->get_journal(plr.id);
    if (!journal)
        return 0;

    // Collect first: complete_quest mutates the journal.
    std::vector<quest_id> ready;
    for (const auto& st : journal->active_quests)
    {
        const auto* t = quests_->get_quest_template(st.template_id);
        if (!t || t->quest_giver != giver)
            continue;
        if ((st.is_active() || st.is_complete()) && st.all_objectives_complete(t->objectives))
            ready.push_back(st.template_id);
    }

    int done = 0;
    for (auto id : ready)
    {
        const auto* t = quests_->get_quest_template(id);
        if (quests_->complete_quest(plr.id, id) == quest::complete_result::success && t)
        {
            completed.push_back({{"quest_id", id.value}, {"name", t->name}, {"rewards", rewards_to_json(t->rewards)}});
            ++done;
        }
    }
    return done;
}

// ========== Request handlers ==========

void game_handlers::handle_quest_list(connection_id conn_id, const network::json_message& msg)
{
    auto data_result = network::quest_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    auto check = validate_npc_interaction(conn_id, msg.seq, data_result.value().npc_entity_id);
    if (!check.valid)
        return;
    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn)
        return;
    if (!quests_)
    {
        conn->send(network::make_quest_list_response(msg.seq, false, data_result.value().npc_entity_id, {}, "quests_unavailable"));
        return;
    }
    conn->send(network::make_quest_list_response(msg.seq,
                                                 true,
                                                 data_result.value().npc_entity_id,
                                                 quests_offered_by(*check.plr, check.target_npc->template_id),
                                                 {}));
}

void game_handlers::handle_quest_accept(connection_id conn_id, const network::json_message& msg)
{
    auto data_result = network::quest_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    const auto& data = data_result.value();
    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid)
        return;
    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn)
        return;

    const auto respond = [&](bool ok, std::string_view err)
    { conn->send(network::make_quest_action_response(network::json_message_type::quest_accept_response, msg.seq, ok, data.quest_id, err)); };

    if (!quests_)
    {
        respond(false, "quests_unavailable");
        return;
    }
    const auto qid = quest_id{data.quest_id};
    const auto* t = quests_->get_quest_template(qid);
    if (!t)
    {
        respond(false, "quest_not_found");
        return;
    }
    if (t->quest_giver != check.target_npc->template_id)
    {
        respond(false, "wrong_npc");
        return;
    }
    const auto level = static_cast<int16_t>(check.plr->experience.level);
    if (level < t->min_level || level > t->max_level)
    {
        respond(false, "level_out_of_range");
        return;
    }
    if (t->required_faction != 0 && t->required_faction != static_cast<uint8_t>(check.plr->faction))
    {
        respond(false, "wrong_faction");
        return;
    }

    switch (quests_->accept_quest(check.plr->id, qid))
    {
    case quest::accept_result::success:
        respond(true, {});
        LOG_DEBUG(bridge, "Player {} accepted quest {} ({})", check.plr->name, qid.value, t->name);
        if (const auto* journal = quests_->get_journal(check.plr->id))
            if (const auto* st = journal->get_quest(qid))
                send_quest_update(*check.plr, *t, *st);
        break;
    case quest::accept_result::already_active:
        respond(false, "already_active");
        break;
    case quest::accept_result::quest_log_full:
        respond(false, "quest_log_full");
        break;
    case quest::accept_result::missing_prerequisite:
        respond(false, "missing_prerequisite");
        break;
    default:
        respond(false, "cannot_accept");
        break;
    }
}

void game_handlers::handle_quest_abandon(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;
    auto data_result = network::quest_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    const auto qid = data_result.value().quest_id;
    const bool ok = quests_ && quests_->abandon_quest(conn->player(), quest_id{qid});
    conn->send(network::make_quest_action_response(
        network::json_message_type::quest_abandon_response, msg.seq, ok, qid, ok ? std::string_view{} : "not_active"));
}

void game_handlers::handle_quest_complete(connection_id conn_id, const network::json_message& msg)
{
    auto data_result = network::quest_request_data::from_json(msg.data);
    if (data_result.is_err())
    {
        send_error(conn_id, msg.seq, "invalid_request", data_result.error());
        return;
    }
    const auto& data = data_result.value();
    auto check = validate_npc_interaction(conn_id, msg.seq, data.npc_entity_id);
    if (!check.valid)
        return;
    auto* conn = ws_server_->get_connection(conn_id);
    if (!conn)
        return;

    const auto respond = [&](bool ok, nlohmann::json rewards, std::string_view err)
    { conn->send(network::make_quest_complete_response(msg.seq, ok, data.quest_id, std::move(rewards), err)); };

    if (!quests_)
    {
        respond(false, {}, "quests_unavailable");
        return;
    }
    const auto qid = quest_id{data.quest_id};
    const auto* t = quests_->get_quest_template(qid);
    if (!t)
    {
        respond(false, {}, "quest_not_found");
        return;
    }
    const auto turn_in = t->turn_in_npc.value != 0 ? t->turn_in_npc : t->quest_giver;
    if (turn_in != check.target_npc->template_id)
    {
        respond(false, {}, "wrong_npc");
        return;
    }

    switch (quests_->complete_quest(check.plr->id, qid))
    {
    case quest::complete_result::success:
        respond(true, rewards_to_json(t->rewards), {});
        break;
    case quest::complete_result::objectives_incomplete:
        respond(false, {}, "objectives_incomplete");
        break;
    case quest::complete_result::not_active:
        respond(false, {}, "not_active");
        break;
    default:
        respond(false, {}, "cannot_complete");
        break;
    }
}

void game_handlers::handle_quest_journal(connection_id conn_id, const network::json_message& msg)
{
    auto* conn = require_in_game(conn_id, msg.seq);
    if (!conn)
        return;
    nlohmann::json quests = nlohmann::json::array();
    if (quests_)
    {
        if (const auto* journal = quests_->get_journal(conn->player()))
        {
            for (const auto& st : journal->active_quests)
            {
                if (const auto* t = quests_->get_quest_template(st.template_id))
                    quests.push_back(quest_to_json(*t, &st));
            }
        }
    }
    conn->send(network::make_quest_journal_response(msg.seq, std::move(quests)));
}

} // namespace hb::bridge
