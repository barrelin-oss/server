// random_spawn_whitelist.cpp
// Implementation of random spawn whitelist

#include "npc/random_spawn_whitelist.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>
#include <algorithm>

namespace hb::npc
{

namespace
{

auto parse_special_attack_kind(std::string_view str) -> special_attack_kind
{
    if (str == "melee" || str == "1")
        return special_attack_kind::melee;
    if (str == "ranged" || str == "2")
        return special_attack_kind::ranged;
    if (str == "poison" || str == "3")
        return special_attack_kind::poison;
    if (str == "stun" || str == "4")
        return special_attack_kind::stun;
    if (str == "magic" || str == "5")
        return special_attack_kind::magic;
    if (str == "area" || str == "6")
        return special_attack_kind::area;
    if (str == "buff" || str == "7")
        return special_attack_kind::buff;
    if (str == "summon" || str == "8")
        return special_attack_kind::summon;
    return special_attack_kind::none;
}

} // namespace

auto random_spawn_whitelist::load_from_yaml(const std::filesystem::path& path) -> result<size_t, std::string>
{
    try
    {
        YAML::Node config = YAML::LoadFile(path.string());

        if (!config["random_spawnable_npcs"])
        {
            return result<size_t, std::string>::err("Missing 'random_spawnable_npcs' key in YAML");
        }

        YAML::Node npcs_node = config["random_spawnable_npcs"];
        return load_from_node(&npcs_node);
    }
    catch (const YAML::Exception& e)
    {
        return result<size_t, std::string>::err(std::string("YAML parsing error: ") + e.what());
    }
    catch (const std::exception& e)
    {
        return result<size_t, std::string>::err(std::string("Error loading whitelist: ") + e.what());
    }
}

auto random_spawn_whitelist::load_from_node(const void* yaml_node) -> result<size_t, std::string>
{
    const auto& node = *static_cast<const YAML::Node*>(yaml_node);

    if (!node.IsSequence())
    {
        return result<size_t, std::string>::err("random_spawnable_npcs must be a sequence");
    }

    clear();

    for (const auto& entry_node : node)
    {
        random_spawn_entry entry;

        // Required: name
        if (!entry_node["name"])
        {
            LOG_WARN(general, "Skipping random spawn entry without name");
            continue;
        }
        entry.name = entry_node["name"].as<std::string>();

        // Optional: template_id (default 0 = not spawnable)
        if (entry_node["template_id"])
        {
            int id = entry_node["template_id"].as<int>(0);
            // Legacy used -1 for "not implemented", we use 0
            entry.template_id = npc_id{id > 0 ? static_cast<uint16_t>(id) : 0};
        }

        // Optional: special_attack_prob (0-100)
        if (entry_node["special_attack_prob"])
        {
            entry.special_attack_prob = entry_node["special_attack_prob"].as<int>(0);
        }

        // Optional: special_attack_kind
        if (entry_node["special_attack_kind"])
        {
            auto kind_str = entry_node["special_attack_kind"].as<std::string>();
            entry.special_attack = parse_special_attack_kind(kind_str);
        }

        // Optional: enabled (default true)
        if (entry_node["enabled"])
        {
            entry.enabled = entry_node["enabled"].as<bool>(true);
        }

        // Add to indices
        size_t idx = entries_.size();
        name_index_[entry.name] = idx;
        if (entry.template_id.value > 0)
        {
            id_index_[entry.template_id.value] = idx;
        }

        entries_.push_back(std::move(entry));
    }

    LOG_INFO(general, "Loaded {} random spawnable NPCs ({} enabled)", entries_.size(), spawnable_count());

    return result<size_t, std::string>::ok(entries_.size());
}

auto random_spawn_whitelist::is_allowed(std::string_view name) const -> bool
{
    auto it = name_index_.find(std::string(name));
    if (it == name_index_.end())
    {
        return false;
    }
    const auto& entry = entries_[it->second];
    return entry.enabled && entry.template_id.value > 0;
}

auto random_spawn_whitelist::is_allowed(npc_id id) const -> bool
{
    auto it = id_index_.find(id.value);
    if (it == id_index_.end())
    {
        return false;
    }
    const auto& entry = entries_[it->second];
    return entry.enabled && entry.template_id.value > 0;
}

auto random_spawn_whitelist::get(std::string_view name) const -> std::optional<random_spawn_entry>
{
    auto it = name_index_.find(std::string(name));
    if (it == name_index_.end())
    {
        return std::nullopt;
    }
    return entries_[it->second];
}

auto random_spawn_whitelist::get_by_id(npc_id id) const -> std::optional<random_spawn_entry>
{
    auto it = id_index_.find(id.value);
    if (it == id_index_.end())
    {
        return std::nullopt;
    }
    return entries_[it->second];
}

auto random_spawn_whitelist::spawnable_count() const -> size_t
{
    return static_cast<size_t>(std::count_if(entries_.begin(),
                                            entries_.end(),
                                            [](const random_spawn_entry& e)
                                            { return e.enabled && e.template_id.value > 0; }));
}

void random_spawn_whitelist::clear()
{
    entries_.clear();
    name_index_.clear();
    id_index_.clear();
}

} // namespace hb::npc
