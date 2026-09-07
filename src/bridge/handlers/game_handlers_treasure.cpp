// game_handlers_treasure.cpp - treasure chests (after the Helbreath Olympia ones). Every interval a
// chest NPC appears on a random walkable tile of one of the configured maps and stays for a while;
// whoever opens it takes the gold and the drops fall around the chest. Silver chests are
// announced to the map, gold chests to everyone. Config: bin/game_configs/treasure_chests.yaml.
#include "platform/platform.h"
#include "bridge/handlers/game_handlers.h"
#include "network/websocket_server.h"
#include "player/player_system.h"
#include "world/world_subsystem.h"
#include "npc/npc_system.h"
#include "npc/npc.h"
#include "npc/loot_generator.h"
#include "registry/npc_registry.h"
#include "registry/loot_registry.h"
#include "inventory/inventory_system.h"
#include "item/item_system.h"
#include "item/item_ops.h"
#include "scheduler/scheduler.h"
#include "achievement/achievement_system.h"
#include "core/logger.h"
#include "core/subsystem.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <random>

namespace hb::bridge
{

namespace
{
auto treasure_rng() -> std::mt19937&
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    return rng;
}

auto treasure_roll(int lo, int hi) -> int
{
    if (lo >= hi)
        return lo;
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(treasure_rng());
}

// "Chest-Gold" -> "gold"
auto tier_label(std::string_view npc_name) -> std::string
{
    std::string label(npc_name.rfind("Chest-", 0) == 0 ? npc_name.substr(6) : npc_name);
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) { return std::tolower(c); });
    return label;
}
} // namespace

void game_handlers::setup_treasure_chests(const std::filesystem::path& config_path)
{
    treasure_ = {};
    if (!std::filesystem::exists(config_path))
    {
        LOG_INFO(bridge, "No treasure_chests.yaml: treasure chests disabled");
        return;
    }
    try
    {
        auto root = YAML::LoadFile(config_path.string());
        auto node = root["treasure_chests"];
        if (!node)
            return;
        treasure_.enabled = node["enabled"].as<bool>(true);
        treasure_.interval_seconds = std::max(1, node["interval_minutes"].as<int>(10)) * 60;
        treasure_.lifetime_seconds = std::max(1, node["lifetime_minutes"].as<int>(15)) * 60;
        for (const auto& m : node["maps"])
            treasure_.maps.push_back(m.as<std::string>());
        for (const auto& t : node["tiers"])
        {
            treasure_.tiers.push_back({.npc_name = t["name"].as<std::string>(),
                                       .weight = std::max(1, t["weight"].as<int>(1)),
                                       .announce = t["announce"].as<std::string>("none"),
                                       .gold_min = t["gold_min"].as<int>(0),
                                       .gold_max = t["gold_max"].as<int>(0)});
        }
    }
    catch (const std::exception& e)
    {
        LOG_ERROR(bridge, "treasure_chests.yaml: {}", e.what());
        treasure_ = {};
        return;
    }
    if (!treasure_.enabled || treasure_.maps.empty() || treasure_.tiers.empty() || !scheduler_)
    {
        treasure_.enabled = false;
        LOG_INFO(bridge, "Treasure chests disabled (config, maps, tiers or scheduler missing)");
        return;
    }
    scheduler_->schedule_repeating(std::chrono::milliseconds(treasure_.interval_seconds) * 1000,
                                   [this]() { spawn_treasure_chest(); });
    LOG_INFO(bridge,
             "Treasure chests: one every {} min on {} maps, gone after {} min",
             treasure_.interval_seconds / 60,
             treasure_.maps.size(),
             treasure_.lifetime_seconds / 60);
}

void game_handlers::announce_treasure(std::string_view scope, map_id map, const std::string& text)
{
    if (scope == "none" || !players_ || !ws_server_)
        return;
    players_->for_each_player(
        [&](player_id, player::player& p)
        {
            if (scope == "map" && p.current_map != map)
                return;
            if (auto* conn = ws_server_->get_connection(p.connection))
            {
                conn->send(network::make_chat_message_broadcast({
                    .channel = "system",
                    .sender_id = 0,
                    .sender_name = "",
                    .content = text,
                    .flags = {"system"},
                }));
            }
        });
}

void game_handlers::spawn_treasure_chest()
{
    if (!treasure_.enabled || !world_ || !npc_)
        return;
    auto* npcs = subsystems().get<npc_registry>();
    if (!npcs)
        return;

    const auto& map_name = treasure_.maps[static_cast<size_t>(treasure_roll(0, static_cast<int>(treasure_.maps.size()) - 1))];
    auto* map = world_->get_map_by_name(map_name);
    if (!map)
    {
        LOG_WARN(bridge, "Treasure chests: map {} is not loaded", map_name);
        return;
    }

    int total = 0;
    for (const auto& t : treasure_.tiers)
        total += t.weight;
    int r = treasure_roll(1, total);
    const treasure_tier* tier = &treasure_.tiers.back();
    for (const auto& t : treasure_.tiers)
    {
        r -= t.weight;
        if (r <= 0)
        {
            tier = &t;
            break;
        }
    }
    const auto* tmpl = npcs->find_by_name(tier->npc_name);
    if (!tmpl)
    {
        LOG_WARN(bridge, "Treasure chests: no NPC template {}", tier->npc_name);
        return;
    }

    for (int attempt = 0; attempt < 60; ++attempt)
    {
        world::position pos{static_cast<int16_t>(treasure_roll(2, map->width() - 3)),
                            static_cast<int16_t>(treasure_roll(2, map->height() - 3))};
        if (!map->is_walkable(pos))
            continue;
        auto spawned = npc_->spawn_npc(tmpl->id, map->id(), pos);
        if (spawned.is_err())
            continue;
        const auto ent = spawned.value();
        if (auto* n = npc_->get_npc(ent))
        {
            n->gold_min = tier->gold_min;
            n->gold_max = tier->gold_max;
        }
        const auto label = tier_label(tier->npc_name);
        announce_treasure(tier->announce, map->id(), std::format("A {} treasure chest has appeared in {}!", label, map_name));
        if (scheduler_)
        {
            const auto lifetime = treasure_.lifetime_seconds;
            scheduler_->schedule(std::chrono::milliseconds(lifetime) * 1000,
                                 [this, ent]()
                                 {
                                     if (npc_ && npc_->get_npc(ent))
                                         npc_->despawn_npc(ent);
                                 });
        }
        LOG_INFO(bridge, "Treasure chest ({}) at {} ({}, {})", label, map_name, pos.x, pos.y);
        return;
    }
    LOG_WARN(bridge, "Treasure chests: no walkable tile found on {}", map_name);
}

// The player clicked a chest within reach: gold to the player, drops around the chest, chest gone.
void game_handlers::open_treasure_chest(network::ws_connection& conn, uint32_t seq, player::player& player, npc::npc& chest)
{
    const auto chest_entity = chest.entity_id;
    const auto chest_id = chest.entity_id.id;
    const auto chest_map = chest.current_map;
    const auto chest_pos = chest.pos;
    const auto chest_name = chest.name;
    const auto label = tier_label(chest_name);

    int64_t gold = 0;
    int items = 0;
    if (loot_registry_ && item_ && world_)
    {
        auto drop = npc::generate_kill_loot(*loot_registry_, chest.sprite_id, chest.gold_min, chest.gold_max, false, 1.0f);
        if (drop.gold > 0 && inventory_)
        {
            const auto owner = entity_id{player.id.value};
            inventory_->add_gold(owner, drop.gold);
            gold = drop.gold;
            conn.send(network::make_gold_update({.gold = static_cast<int64_t>(inventory_->get_gold(owner)),
                                                 .change = static_cast<int64_t>(drop.gold),
                                                 .reason = "treasure"}));
        }
        for (const auto& loot_item : drop.items)
        {
            auto created = item_->create_from_template(loot_item.template_id, 1);
            if (created.is_err())
                continue;
            auto dropped = created.value();
            if (!loot_item.attribute.is_empty())
            {
                if (auto* itm = item_->get_item(dropped))
                    itm->attribute = loot_item.attribute;
            }
            const world::position at{static_cast<int16_t>(chest_pos.x + treasure_roll(-1, 1)),
                                     static_cast<int16_t>(chest_pos.y + treasure_roll(-1, 1))};
            item_ops::drop_loot(dropped, chest_map, at.x, at.y, item_, world_);
            broadcast_ground_item_spawn(chest_map, at, dropped);
            ++items;
        }
    }

    if (npc_)
        npc_->despawn_npc(chest_entity);

    if (achievements_)
    {
        grant_achievements(player, achievements_->add(player.id, achievement::counter_kind::chests_opened, 0, 1));
        if (gold > 0)
            grant_achievements(player, achievements_->add(player.id, achievement::counter_kind::gold_looted, 0, gold));
    }

    announce_treasure("map", chest_map, std::format("{} opened a {} treasure chest!", player.name, label));
    LOG_INFO(bridge, "{} opened a {} chest: {} gold, {} items", player.name, label, gold, items);

    network::interact_result_msg result;
    result.success = true;
    result.target_id = chest_id;
    result.interaction_type = "treasure";
    result.interaction_data = nlohmann::json{{"tier", label}, {"gold", gold}, {"items", items}};
    conn.send(network::make_player_interact_response(seq, true, &result));
}

} // namespace hb::bridge
