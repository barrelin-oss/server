// specialty_system.cpp - monster mastery (see specialty_system.h)
#include "specialty/specialty_system.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace hb::specialty
{

auto bonus_kind_from_string(std::string_view s) -> std::optional<bonus_kind>
{
    if (s == "damage")
        return bonus_kind::damage;
    if (s == "damage_pct")
        return bonus_kind::damage_pct;
    if (s == "damage_reduction")
        return bonus_kind::damage_reduction;
    if (s == "damage_reduction_pct")
        return bonus_kind::damage_reduction_pct;
    if (s == "hit_ratio")
        return bonus_kind::hit_ratio;
    if (s == "hit_ratio_pct")
        return bonus_kind::hit_ratio_pct;
    if (s == "drop_rate")
        return bonus_kind::drop_rate;
    return std::nullopt;
}

auto to_string(bonus_kind k) -> std::string_view
{
    switch (k)
    {
    case bonus_kind::damage:
        return "damage";
    case bonus_kind::damage_pct:
        return "damage_pct";
    case bonus_kind::damage_reduction:
        return "damage_reduction";
    case bonus_kind::damage_reduction_pct:
        return "damage_reduction_pct";
    case bonus_kind::hit_ratio:
        return "hit_ratio";
    case bonus_kind::hit_ratio_pct:
        return "hit_ratio_pct";
    case bonus_kind::drop_rate:
        return "drop_rate";
    }
    return "unknown";
}

auto describe_step(bonus_kind k) -> std::string
{
    switch (k)
    {
    case bonus_kind::damage:
        return std::format("+{} damage", damage_per_step);
    case bonus_kind::damage_pct:
        return std::format("+{}% damage", damage_pct_per_step);
    case bonus_kind::damage_reduction:
        return std::format("+{}% damage reduction", reduction_per_step);
    case bonus_kind::damage_reduction_pct:
        return std::format("-{}% damage taken", reduction_pct_per_step);
    case bonus_kind::hit_ratio:
        return std::format("+{} hit ratio", hit_ratio_per_step);
    case bonus_kind::hit_ratio_pct:
        return std::format("+{}% hit ratio", hit_ratio_pct_per_step);
    case bonus_kind::drop_rate:
        return std::format("+{}% drop rate", drop_rate_pct_per_step);
    }
    return {};
}

auto specialty_bonuses::any() const -> bool
{
    return damage != 0 || damage_mult != 1.0f || reduction_pct != 0 || reduction_mult != 1.0f || hit_ratio != 0 ||
           hit_mult != 1.0f || drop_mult != 1.0f;
}

void specialty_system::initialize()
{
    set_initialized(true);
    LOG_INFO(general, "Specialty system initialized");
}

void specialty_system::shutdown()
{
    kills_.clear();
    set_initialized(false);
}

auto specialty_system::load_from_file(const std::string& path) -> result<size_t, std::string>
{
    using R = result<size_t, std::string>;
    YAML::Node root;
    try
    {
        root = YAML::LoadFile(path);
    }
    catch (const std::exception& e)
    {
        return R::err(std::format("{}: {}", path, e.what()));
    }
    return load_from_yaml(YAML::Dump(root));
}

auto specialty_system::load_from_yaml(const std::string& text) -> result<size_t, std::string>
{
    using R = result<size_t, std::string>;
    YAML::Node root;
    try
    {
        root = YAML::Load(text);
    }
    catch (const std::exception& e)
    {
        return R::err(std::format("specialties: {}", e.what()));
    }
    const auto list = root["specialties"];
    if (!list || !list.IsSequence())
        return R::err("specialties: missing 'specialties' sequence");

    defs_.clear();
    by_type_.clear();
    for (const auto& row : list)
    {
        specialty_def def;
        def.npc_type = static_cast<int16_t>(row["npc_type"].as<int>(0));
        def.npc_name = row["npc_name"].as<std::string>("");
        def.base_kills = row["base_kills"].as<int>(0);
        if (def.npc_type <= 0 || def.base_kills <= 0)
            return R::err(std::format("specialties: row for '{}' needs npc_type and base_kills", def.npc_name));
        for (const auto& step : row["ladder"])
        {
            auto kind = bonus_kind_from_string(step.as<std::string>());
            if (!kind)
                return R::err(std::format("specialties: unknown bonus '{}' for {}", step.as<std::string>(), def.npc_name));
            def.ladder.push_back(*kind);
        }
        if (def.ladder.empty())
            return R::err(std::format("specialties: {} has an empty ladder", def.npc_name));
        by_type_[def.npc_type] = defs_.size();
        defs_.push_back(std::move(def));
    }
    LOG_INFO(general, "Loaded {} specialties", defs_.size());
    return R::ok(defs_.size());
}

auto specialty_system::find(int16_t npc_type) const -> const specialty_def*
{
    auto it = by_type_.find(npc_type);
    return it == by_type_.end() ? nullptr : &defs_[it->second];
}

void specialty_system::register_player(player_id id)
{
    kills_.try_emplace(id.value);
}

void specialty_system::unregister_player(player_id id)
{
    kills_.erase(id.value);
}

auto specialty_system::kills_for_level(int32_t base_kills, int32_t level) -> int32_t
{
    if (level <= 0)
        return 0;
    return base_kills * level * (level + 1) / 2;
}

auto specialty_system::level_for(int32_t kills, int32_t base_kills, int32_t max_level) -> int32_t
{
    int32_t level = 0;
    while (level < max_level && kills >= kills_for_level(base_kills, level + 1))
        ++level;
    return level;
}

auto specialty_system::bonuses_for(const specialty_def& def, int32_t level) -> specialty_bonuses
{
    specialty_bonuses b;
    const auto steps = static_cast<size_t>(std::clamp<int32_t>(level, 0, static_cast<int32_t>(def.ladder.size())));
    for (size_t i = 0; i < steps; ++i)
    {
        switch (def.ladder[i])
        {
        case bonus_kind::damage:
            b.damage += damage_per_step;
            break;
        case bonus_kind::damage_pct:
            b.damage_mult *= 1.0f + damage_pct_per_step / 100.0f;
            break;
        case bonus_kind::damage_reduction:
            b.reduction_pct += reduction_per_step;
            break;
        case bonus_kind::damage_reduction_pct:
            b.reduction_mult *= 1.0f + reduction_pct_per_step / 100.0f;
            break;
        case bonus_kind::hit_ratio:
            b.hit_ratio += hit_ratio_per_step;
            break;
        case bonus_kind::hit_ratio_pct:
            b.hit_mult *= 1.0f + hit_ratio_pct_per_step / 100.0f;
            break;
        case bonus_kind::drop_rate:
            b.drop_mult *= 1.0f + drop_rate_pct_per_step / 100.0f;
            break;
        }
    }
    return b;
}

auto specialty_system::make_progress(const specialty_def& def, int32_t kills) const -> specialty_progress
{
    specialty_progress p;
    p.npc_type = def.npc_type;
    p.npc_name = def.npc_name;
    p.kills = kills;
    p.max_level = static_cast<int32_t>(def.ladder.size());
    p.level = level_for(kills, def.base_kills, p.max_level);
    p.next_level_kills = p.level < p.max_level ? kills_for_level(def.base_kills, p.level + 1) : 0;
    p.unlocked.assign(def.ladder.begin(), def.ladder.begin() + p.level);
    return p;
}

auto specialty_system::record_kill(player_id id, int16_t npc_type) -> std::optional<specialty_progress>
{
    const auto* def = find(npc_type);
    if (!def)
        return std::nullopt;
    auto& per_type = kills_[id.value];
    auto& count = per_type[npc_type];
    const auto before = level_for(count, def->base_kills, static_cast<int32_t>(def->ladder.size()));
    ++count;
    const auto after = level_for(count, def->base_kills, static_cast<int32_t>(def->ladder.size()));
    if (after == before)
        return std::nullopt;
    return make_progress(*def, count);
}

auto specialty_system::kills(player_id id, int16_t npc_type) const -> int32_t
{
    auto pit = kills_.find(id.value);
    if (pit == kills_.end())
        return 0;
    auto it = pit->second.find(npc_type);
    return it == pit->second.end() ? 0 : it->second;
}

auto specialty_system::bonuses(player_id id, int16_t npc_type) const -> specialty_bonuses
{
    const auto* def = find(npc_type);
    if (!def)
        return {};
    const auto k = kills(id, npc_type);
    if (k <= 0)
        return {};
    return bonuses_for(*def, level_for(k, def->base_kills, static_cast<int32_t>(def->ladder.size())));
}

auto specialty_system::progress_for(player_id id, int16_t npc_type) const -> std::optional<specialty_progress>
{
    const auto* def = find(npc_type);
    if (!def)
        return std::nullopt;
    return make_progress(*def, kills(id, npc_type));
}

auto specialty_system::progress(player_id id) const -> std::vector<specialty_progress>
{
    std::vector<specialty_progress> out;
    auto pit = kills_.find(id.value);
    if (pit == kills_.end())
        return out;
    for (const auto& [type, count] : pit->second)
    {
        if (count <= 0)
            continue;
        if (const auto* def = find(type))
            out.push_back(make_progress(*def, count));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.kills > b.kills; });
    return out;
}

auto specialty_system::serialize(player_id id) const -> std::string
{
    nlohmann::json arr = nlohmann::json::array();
    if (auto pit = kills_.find(id.value); pit != kills_.end())
    {
        for (const auto& [type, count] : pit->second)
        {
            if (count > 0)
                arr.push_back({{"type", type}, {"kills", count}});
        }
    }
    return arr.dump();
}

void specialty_system::deserialize(player_id id, const std::string& json_text)
{
    auto& per_type = kills_[id.value];
    per_type.clear();
    if (json_text.empty())
        return;
    auto parsed = nlohmann::json::parse(json_text, nullptr, false);
    if (!parsed.is_array())
    {
        LOG_WARN(general, "specialty_data of player {} is not an array; ignored", id.value);
        return;
    }
    for (const auto& e : parsed)
    {
        if (!e.is_object())
            continue;
        const auto type = static_cast<int16_t>(e.value("type", 0));
        const auto count = e.value("kills", 0);
        if (type > 0 && count > 0)
            per_type[type] = count;
    }
}

} // namespace hb::specialty
