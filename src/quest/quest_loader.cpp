// quest_loader.cpp
#include "quest/quest_loader.h"

#include "core/logger.h"
#include "npc/spot_mob_mapping.h"
#include "quest/quest_system.h"
#include "registry/npc_registry.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <format>

namespace hb::quest
{

namespace
{

template<typename T> auto field(const YAML::Node& row, const char* key, T fallback) -> T
{
    if (auto n = row[key]; n && !n.IsNull())
        return n.as<T>();
    return fallback;
}

// Legacy: side 1 = Aresden, 2 = Elvine. The city hall officers are the only quest givers.
auto city_hall_officer_for_side(int side) -> std::string_view
{
    return side == 2 ? "William" : "Kennedy";
}

auto home_map_for_side(int side) -> std::string_view
{
    return side == 2 ? "elvine" : "aresden";
}

auto side_name(int side) -> std::string_view
{
    return side == 2 ? "Elvine" : "Aresden";
}

void add_legacy_reward(quest_rewards& rewards, int type, int amount, int16_t min_level)
{
    if (amount <= 0 || type == 0)
        return;
    if (type == legacy_reward_exp)
    {
        rewards.experience += amount;
    }
    else if (type == legacy_reward_scaled_exp)
    {
        // Legacy "scaled exp" was multiplied by the player level at turn-in. Templates
        // are immutable, so scale by the quest minimum level: a deliberate under-estimate.
        rewards.experience += static_cast<int64_t>(amount) * std::max<int16_t>(1, min_level);
    }
    else if (type == legacy_gold_item_id)
    {
        rewards.gold += amount;
    }
    else if (type > 0)
    {
        rewards.items.push_back(
            item_reward{.item_type = item_id{static_cast<uint16_t>(type)}, .count = static_cast<int16_t>(amount)});
    }
}

// gather_item/gather_count (and _2, _3): items the player must bring back. Returns an error text.
auto add_gather_objectives(const YAML::Node& row, quest_template& t) -> std::string
{
    for (const char* suffix : {"", "_2", "_3"})
    {
        const auto item_key = std::string("gather_item") + suffix;
        const auto count_key = std::string("gather_count") + suffix;
        const int item = field<int>(row, item_key.c_str(), 0);
        if (item <= 0)
            continue;
        const int count = field<int>(row, count_key.c_str(), 1);
        if (count <= 0)
            return std::format("{} needs a positive {}", item_key, count_key);
        objective_template obj;
        obj.id = static_cast<uint8_t>(t.objectives.size());
        obj.type = objective_type::collect_item;
        obj.description = std::format("Bring {} x item {}", count, item);
        obj.data = collect_objective_data{
            .item_type = item_id{static_cast<uint16_t>(item)}, .required_count = count, .deliver_to = npc_id{}};
        t.objectives.push_back(std::move(obj));
    }
    return {};
}
} // namespace

auto legacy_row_to_template(const YAML::Node& row,
                            const npc_registry& npcs,
                            const map_resolver& resolve_map) -> result<quest_template, std::string>
{
    using R = result<quest_template, std::string>;

    const int id = field<int>(row, "id", 0);
    if (id <= 0)
        return R::err("row without a positive id");

    const int side = field<int>(row, "side", 0);
    const int type = field<int>(row, "type", 0);
    const int min_level = field<int>(row, "min_level", 1);
    const int max_level = field<int>(row, "max_level", 300);

    quest_template t;
    t.id = quest_id{static_cast<uint16_t>(id)};
    t.type = quest_type::side;
    t.min_level = static_cast<int16_t>(min_level);
    t.max_level = static_cast<int16_t>(max_level);
    t.required_faction = static_cast<uint8_t>(side);
    t.repeatable = true; // legacy hunting quests could be taken again after turn-in
    t.time_limit_seconds = std::max(0, field<int>(row, "time_limit", -1));
    // period_hours: a daily quest waits that long after a turn-in (Olympia's period)
    t.repeat_after_seconds = std::max(0, field<int>(row, "period_hours", 0)) * 3600;
    if (t.repeat_after_seconds > 0)
        t.type = quest_type::daily;

    const auto* giver = npcs.find_by_name(city_hall_officer_for_side(side));
    if (!giver)
        return R::err(
            std::format("quest {}: city hall officer {} not in npc registry", id, city_hall_officer_for_side(side)));
    t.quest_giver = giver->id;
    t.quest_giver_map = resolve_map(home_map_for_side(side));
    // Optional: a named quest giver (the Olympia persons) instead of the city hall officer,
    // standing on giver_map (defaults to the nation's home map)
    if (const auto giver_name = field<std::string>(row, "giver", ""); !giver_name.empty())
    {
        const auto* named = npcs.find_by_name(giver_name);
        if (!named)
            return R::err(std::format("quest {}: giver {} not in npc registry", id, giver_name));
        t.quest_giver = named->id;
        if (const auto giver_map = field<std::string>(row, "giver_map", ""); !giver_map.empty())
            t.quest_giver_map = resolve_map(giver_map);
    }

    const std::string map_name = field<std::string>(row, "map", "");

    if (type == legacy_quest_type_hunt)
    {
        const int target_type = field<int>(row, "target_type", 0);
        const int count = field<int>(row, "max_count", 0);
        auto target_name = npc::spot_mob_type_to_name(target_type);
        if (!target_name)
            return R::err(std::format("quest {}: unknown legacy target_type {}", id, target_type));
        const auto* target = npcs.find_by_name(*target_name);
        if (!target)
            return R::err(std::format("quest {}: target {} not in npc registry", id, *target_name));
        if (count <= 0)
            return R::err(std::format("quest {}: max_count must be positive", id));

        t.name = std::format("Hunt {} x{}", target->name, count);
        t.description = std::format("Hunt {} {} for {} (level {}-{}){}",
                                    count,
                                    target->name,
                                    side_name(side),
                                    min_level,
                                    max_level,
                                    map_name.empty() ? std::string{} : std::format(", around {}", map_name));

        const bool elite = field<bool>(row, "elite", false);
        objective_template obj;
        obj.id = 0;
        obj.type = objective_type::kill_monster;
        obj.description = std::format("Kill {} {}{}", count, elite ? "Elite " : "", target->name);
        obj.data = kill_objective_data{
            .target_type = target->id, .required_count = count, .player_kills = false, .elite_only = elite};
        t.objectives.push_back(std::move(obj));
        if (const int target2 = field<int>(row, "target_type2", 0); target2 > 0)
        {
            const int count2 = field<int>(row, "max_count2", 0);
            auto name2 = npc::spot_mob_type_to_name(target2);
            const auto* second = name2 ? npcs.find_by_name(*name2) : nullptr;
            if (!second || count2 <= 0)
                return R::err(std::format("quest {}: bad second target {} x{}", id, target2, count2));
            objective_template obj2;
            obj2.id = static_cast<uint8_t>(t.objectives.size());
            obj2.type = objective_type::kill_monster;
            const bool elite2 = field<bool>(row, "elite2", false);
            obj2.description = std::format("Kill {} {}{}", count2, elite2 ? "Elite " : "", second->name);
            obj2.data = kill_objective_data{
                .target_type = second->id, .required_count = count2, .player_kills = false, .elite_only = elite2};
            t.objectives.push_back(std::move(obj2));
        }
        if (auto err = add_gather_objectives(row, t); !err.empty())
            return R::err(std::format("quest {}: {}", id, err));
    }
    else if (type == legacy_quest_type_gather)
    {
        t.name = "Gather";
        t.description = std::format("Gather what is asked for {} (level {}-{})", side_name(side), min_level, max_level);
        if (auto err = add_gather_objectives(row, t); !err.empty())
            return R::err(std::format("quest {}: {}", id, err));
        if (t.objectives.empty())
            return R::err(std::format("quest {}: gather row without gather_item", id));
    }
    else if (type == legacy_quest_type_goplace)
    {
        const auto target_map = resolve_map(map_name);
        if (target_map.value == 0)
            return R::err(std::format("quest {}: map {} is not loaded", id, map_name));
        const int x = field<int>(row, "x", 0);
        const int y = field<int>(row, "y", 0);
        const int range = std::max(1, field<int>(row, "range", 1));

        t.name = std::format("Scout {}", map_name);
        t.description = std::format(
            "Reach {} ({}, {}) for {} (level {}-{})", map_name, x, y, side_name(side), min_level, max_level);

        objective_template obj;
        obj.id = 0;
        obj.type = objective_type::visit_location;
        obj.description = std::format("Reach {} ({}, {})", map_name, x, y);
        obj.data = location_objective_data{.target_map = target_map,
                                           .target_x = static_cast<int16_t>(x),
                                           .target_y = static_cast<int16_t>(y),
                                           .radius = static_cast<int16_t>(range)};
        t.objectives.push_back(std::move(obj));
    }
    else
    {
        return R::err(std::format("quest {}: unsupported legacy type {}", id, type));
    }

    if (const auto name = field<std::string>(row, "name", ""); !name.empty())
        t.name = name;
    if (const auto description = field<std::string>(row, "description", ""); !description.empty())
        t.description = description;
    for (int i = 1; i <= 3; ++i)
    {
        const auto type_key = std::format("reward_type{}", i);
        const auto amount_key = std::format("reward_amount{}", i);
        add_legacy_reward(
            t.rewards, field<int>(row, type_key.c_str(), 0), field<int>(row, amount_key.c_str(), 0), t.min_level);
    }
    if (const int contribution = field<int>(row, "contribution", 0); contribution > 0)
    {
        t.rewards.reputation.push_back(
            reputation_reward{.faction_id = static_cast<uint8_t>(side), .amount = contribution});
    }

    return R::ok(std::move(t));
}

auto load_legacy_quests(quest_system& quests,
                        const std::filesystem::path& yaml_path,
                        const npc_registry& npcs,
                        const map_resolver& resolve_map) -> result<size_t, std::string>
{
    using R = result<size_t, std::string>;
    YAML::Node root;
    try
    {
        root = YAML::LoadFile(yaml_path.string());
    }
    catch (const std::exception& e)
    {
        return R::err(std::format("{}: {}", yaml_path.string(), e.what()));
    }

    const auto rows = root["quests"];
    if (!rows || !rows.IsSequence())
        return R::err(std::format("{}: missing quests sequence", yaml_path.string()));

    size_t loaded = 0;
    size_t skipped = 0;
    for (const auto& row : rows)
    {
        auto converted = legacy_row_to_template(row, npcs, resolve_map);
        if (converted.is_err())
        {
            LOG_WARN(general, "quests.yaml: skipping {}", converted.error());
            ++skipped;
            continue;
        }
        quests.register_quest(std::move(converted.value()));
        ++loaded;
    }
    if (skipped > 0)
        LOG_WARN(general, "quests.yaml: {} rows skipped (see warnings above)", skipped);
    return R::ok(loaded);
}

} // namespace hb::quest
