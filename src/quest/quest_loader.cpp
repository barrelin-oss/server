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

    const auto* giver = npcs.find_by_name(city_hall_officer_for_side(side));
    if (!giver)
        return R::err(
            std::format("quest {}: city hall officer {} not in npc registry", id, city_hall_officer_for_side(side)));
    t.quest_giver = giver->id;
    t.quest_giver_map = resolve_map(home_map_for_side(side));

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

        objective_template obj;
        obj.id = 0;
        obj.type = objective_type::kill_monster;
        obj.description = std::format("Kill {} {}", count, target->name);
        obj.data = kill_objective_data{.target_type = target->id, .required_count = count, .player_kills = false};
        t.objectives.push_back(std::move(obj));
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
