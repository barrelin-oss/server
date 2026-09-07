// achievement_system.cpp - achievements (see achievement_system.h)
#include "achievement/achievement_system.h"
#include "core/logger.h"

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <chrono>
#include <format>

namespace hb::achievement
{

namespace
{
auto now_seconds() -> int64_t
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

auto counter_kind_from_string(std::string_view s) -> std::optional<counter_kind>
{
    if (s == "kills_total")
        return counter_kind::kills_total;
    if (s == "kills_type")
        return counter_kind::kills_type;
    if (s == "elites_killed")
        return counter_kind::elites_killed;
    if (s == "quests_completed")
        return counter_kind::quests_completed;
    if (s == "gold_looted")
        return counter_kind::gold_looted;
    if (s == "chests_opened")
        return counter_kind::chests_opened;
    if (s == "level")
        return counter_kind::level;
    if (s == "specialties_leveled")
        return counter_kind::specialties_leveled;
    return std::nullopt;
}

auto to_string(counter_kind k) -> std::string_view
{
    switch (k)
    {
    case counter_kind::kills_total:
        return "kills_total";
    case counter_kind::kills_type:
        return "kills_type";
    case counter_kind::elites_killed:
        return "elites_killed";
    case counter_kind::quests_completed:
        return "quests_completed";
    case counter_kind::gold_looted:
        return "gold_looted";
    case counter_kind::chests_opened:
        return "chests_opened";
    case counter_kind::level:
        return "level";
    case counter_kind::specialties_leveled:
        return "specialties_leveled";
    }
    return "unknown";
}

auto achievement_system::counter_key(counter_kind kind, int32_t param) -> std::string
{
    if (kind == counter_kind::kills_type)
        return std::format("kills_type:{}", param);
    return std::string(to_string(kind));
}

void achievement_system::initialize()
{
    set_initialized(true);
    LOG_INFO(general, "Achievement system initialized");
}

void achievement_system::shutdown()
{
    players_.clear();
    set_initialized(false);
}

auto achievement_system::load_from_file(const std::string& path) -> result<size_t, std::string>
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

auto achievement_system::load_from_yaml(const std::string& text) -> result<size_t, std::string>
{
    using R = result<size_t, std::string>;
    YAML::Node root;
    try
    {
        root = YAML::Load(text);
    }
    catch (const std::exception& e)
    {
        return R::err(std::format("achievements: {}", e.what()));
    }
    const auto list = root["achievements"];
    if (!list || !list.IsSequence())
        return R::err("achievements: missing 'achievements' sequence");

    defs_.clear();
    by_id_.clear();
    for (const auto& row : list)
    {
        achievement_def def;
        def.id = static_cast<uint16_t>(row["id"].as<int>(0));
        def.name = row["name"].as<std::string>("");
        def.description = row["description"].as<std::string>("");
        def.category = row["category"].as<std::string>("general");
        def.title = row["title"].as<std::string>("");
        auto kind = counter_kind_from_string(row["kind"].as<std::string>(""));
        if (def.id == 0 || def.name.empty() || !kind)
            return R::err(std::format("achievements: row '{}' needs id, name and a known kind", def.name));
        def.kind = *kind;
        def.param = row["param"].as<int>(0);
        def.target = row["target"].as<int64_t>(1);
        def.points = row["points"].as<int>(10);
        if (def.target <= 0)
            return R::err(std::format("achievements: {} needs a positive target", def.name));
        if (by_id_.contains(def.id))
            return R::err(std::format("achievements: duplicate id {}", def.id));
        by_id_[def.id] = defs_.size();
        defs_.push_back(std::move(def));
    }
    LOG_INFO(general, "Loaded {} achievements", defs_.size());
    return R::ok(defs_.size());
}

auto achievement_system::find(uint16_t id) const -> const achievement_def*
{
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &defs_[it->second];
}

void achievement_system::register_player(player_id id)
{
    players_.try_emplace(id.value);
}

void achievement_system::unregister_player(player_id id)
{
    players_.erase(id.value);
}

auto achievement_system::check_unlocks(player_id id, player_state& st, counter_kind kind, int32_t param)
    -> std::vector<const achievement_def*>
{
    std::vector<const achievement_def*> unlocked;
    const auto key = counter_key(kind, param);
    const auto value = st.counters[key];
    for (const auto& def : defs_)
    {
        if (def.kind != kind || (kind == counter_kind::kills_type && def.param != param))
            continue;
        if (st.unlocked.contains(def.id) || value < def.target)
            continue;
        st.unlocked[def.id] = now_seconds();
        unlocked.push_back(&def);
        LOG_INFO(general, "Player {} unlocked achievement {} ({})", id.value, def.id, def.name);
    }
    return unlocked;
}

auto achievement_system::add(player_id id, counter_kind kind, int32_t param, int64_t amount)
    -> std::vector<const achievement_def*>
{
    if (amount <= 0)
        return {};
    auto& st = players_[id.value];
    st.counters[counter_key(kind, param)] += amount;
    return check_unlocks(id, st, kind, param);
}

auto achievement_system::set_level(player_id id, int32_t level) -> std::vector<const achievement_def*>
{
    auto& st = players_[id.value];
    auto& cur = st.counters[counter_key(counter_kind::level, 0)];
    if (level <= cur)
        return {};
    cur = level;
    return check_unlocks(id, st, counter_kind::level, 0);
}

auto achievement_system::counter(player_id id, counter_kind kind, int32_t param) const -> int64_t
{
    auto pit = players_.find(id.value);
    if (pit == players_.end())
        return 0;
    auto it = pit->second.counters.find(counter_key(kind, param));
    return it == pit->second.counters.end() ? 0 : it->second;
}

auto achievement_system::is_unlocked(player_id id, uint16_t achievement) const -> bool
{
    auto pit = players_.find(id.value);
    return pit != players_.end() && pit->second.unlocked.contains(achievement);
}

auto achievement_system::points(player_id id) const -> int32_t
{
    auto pit = players_.find(id.value);
    if (pit == players_.end())
        return 0;
    int32_t total = 0;
    for (const auto& [aid, at] : pit->second.unlocked)
    {
        if (const auto* def = find(aid))
            total += def->points;
    }
    return total;
}

auto achievement_system::progress(player_id id) const -> std::vector<achievement_progress>
{
    std::vector<achievement_progress> out;
    out.reserve(defs_.size());
    auto pit = players_.find(id.value);
    for (const auto& def : defs_)
    {
        achievement_progress p;
        p.def = &def;
        if (pit != players_.end())
        {
            auto cit = pit->second.counters.find(counter_key(def.kind, def.param));
            p.current = cit == pit->second.counters.end() ? 0 : std::min(cit->second, def.target);
            auto uit = pit->second.unlocked.find(def.id);
            p.unlocked_at = uit == pit->second.unlocked.end() ? 0 : uit->second;
        }
        out.push_back(p);
    }
    return out;
}

auto achievement_system::serialize(player_id id) const -> std::string
{
    nlohmann::json j = {{"counters", nlohmann::json::object()}, {"unlocked", nlohmann::json::array()}};
    if (auto pit = players_.find(id.value); pit != players_.end())
    {
        for (const auto& [key, value] : pit->second.counters)
        {
            if (value > 0)
                j["counters"][key] = value;
        }
        for (const auto& [aid, at] : pit->second.unlocked)
            j["unlocked"].push_back({{"id", aid}, {"at", at}});
    }
    return j.dump();
}

void achievement_system::deserialize(player_id id, const std::string& json_text)
{
    auto& st = players_[id.value];
    st = {};
    if (json_text.empty())
        return;
    auto parsed = nlohmann::json::parse(json_text, nullptr, false);
    if (!parsed.is_object())
    {
        LOG_WARN(general, "achievement_data of player {} is not an object; ignored", id.value);
        return;
    }
    if (parsed.contains("counters") && parsed["counters"].is_object())
    {
        for (const auto& [key, value] : parsed["counters"].items())
        {
            if (value.is_number_integer())
                st.counters[key] = value.get<int64_t>();
        }
    }
    if (parsed.contains("unlocked") && parsed["unlocked"].is_array())
    {
        for (const auto& e : parsed["unlocked"])
        {
            if (e.is_object() && e.value("id", 0) > 0)
                st.unlocked[static_cast<uint16_t>(e.value("id", 0))] = e.value("at", int64_t{0});
        }
    }
}

} // namespace hb::achievement
