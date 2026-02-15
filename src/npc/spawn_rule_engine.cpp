#include "spawn_rule_engine.h"
#include "random_spawn_whitelist.h"
#include "registry/npc_registry.h"
#include "core/subsystem.h"
#include "core/logger.h"
#include <yaml-cpp/yaml.h>
#include <random>
#include <algorithm>

namespace hb::npc
{

// Forward declarations for helper functions
static auto parse_rule(const YAML::Node& node,
                       npc_registry* registry,
                       const random_spawn_whitelist* whitelist) -> std::unique_ptr<spawn_rule>;

spawn_rule_engine::spawn_rule_engine() : rng_(std::random_device{}()) {}

void spawn_rule_engine::initialize()
{
    LOG_INFO(general, "Spawn rule engine initialized");
}

void spawn_rule_engine::shutdown()
{
    global_rules_.clear();
    map_rule_indices_.clear();
    whitelist_.clear();
}

auto spawn_rule_engine::load_from_file(const std::filesystem::path& path) -> result<size_t, std::string>
{
    config_path_ = path;

    try
    {
        YAML::Node config = YAML::LoadFile(path.string());

        // Clear existing rules
        global_rules_.clear();
        map_rule_indices_.clear();
        whitelist_.clear();

        // Load random spawn whitelist (optional but recommended)
        if (config["random_spawnable_npcs"])
        {
            YAML::Node whitelist_node = config["random_spawnable_npcs"];
            auto whitelist_result = whitelist_.load_from_node(&whitelist_node);
            if (whitelist_result.is_err())
            {
                LOG_WARN(general, "Failed to load random spawn whitelist: {}", whitelist_result.error());
            }
        }
        else
        {
            LOG_WARN(general,
                     "No random_spawnable_npcs section found - "
                     "all NPCs will be allowed for random spawning");
        }

        // Parse spawn tables
        if (!config["spawn_tables"])
        {
            return result<size_t, std::string>::err("Missing 'spawn_tables' key in YAML");
        }

        auto* registry = subsystems().get<npc_registry>();
        if (!registry)
        {
            return result<size_t, std::string>::err("NPC registry not available");
        }

        // Determine whitelist to use for filtering
        const random_spawn_whitelist* wl = whitelist_.spawnable_count() > 0 ? &whitelist_ : nullptr;

        size_t rule_count = 0;
        for (const auto& table_node : config["spawn_tables"])
        {
            auto rule = parse_rule(table_node, registry, wl);
            if (!rule)
            {
                LOG_WARN(general, "Failed to parse spawn table, skipping");
                continue;
            }

            global_rules_.push_back(std::move(rule));
            ++rule_count;
        }

        // Parse map-specific spawn tables
        if (config["map_spawn_tables"])
        {
            for (const auto& map_entry : config["map_spawn_tables"])
            {
                std::string map_name = map_entry.first.as<std::string>();
                const auto& tables = map_entry.second["tables"];

                if (!tables.IsSequence())
                {
                    continue;
                }

                std::vector<size_t> indices;
                for (const auto& table_name : tables)
                {
                    std::string name = table_name.as<std::string>();

                    // Find rule index by name
                    for (size_t i = 0; i < global_rules_.size(); ++i)
                    {
                        if (global_rules_[i]->name() == name)
                        {
                            indices.push_back(i);
                            break;
                        }
                    }
                }

                if (!indices.empty())
                {
                    map_rule_indices_[map_name] = std::move(indices);
                }
            }
        }

        LOG_INFO(general, "Loaded {} spawn rules from {}", rule_count, path.string());
        return result<size_t, std::string>::ok(rule_count);
    }
    catch (const YAML::Exception& e)
    {
        return result<size_t, std::string>::err(std::string("YAML parsing error: ") + e.what());
    }
    catch (const std::exception& e)
    {
        return result<size_t, std::string>::err(std::string("Error loading spawn tables: ") + e.what());
    }
}

auto spawn_rule_engine::reload() -> result<void, std::string>
{
    if (config_path_.empty())
    {
        return result<void, std::string>::err("No config file path set");
    }

    auto load_result = load_from_file(config_path_);
    if (load_result.is_err())
    {
        return result<void, std::string>::err(load_result.error());
    }

    LOG_INFO(general, "Reloaded {} spawn rules", load_result.value());
    return result<void, std::string>::ok();
}

auto spawn_rule_engine::select_npc(const spawn_context& ctx) const -> std::optional<npc_id>
{
    auto entries = get_matching_npcs(ctx);
    if (entries.empty())
    {
        return std::nullopt;
    }

    return select_from_weighted(entries);
}

auto spawn_rule_engine::get_matching_npcs(const spawn_context& ctx) const -> std::vector<weighted_npc_entry>
{
    std::vector<std::vector<weighted_npc_entry>> matching_lists;

    // Check if map has specific spawn tables
    auto it = map_rule_indices_.find(std::string(ctx.map_name));
    if (it != map_rule_indices_.end())
    {
        // Use map-specific rules
        for (size_t idx : it->second)
        {
            if (idx < global_rules_.size())
            {
                auto result = global_rules_[idx]->evaluate(ctx);
                if (result.matches && !result.npcs.empty())
                {
                    matching_lists.push_back(std::move(result.npcs));
                }
            }
        }
    }
    else
    {
        // Use all global rules
        for (const auto& rule : global_rules_)
        {
            auto result = rule->evaluate(ctx);
            if (result.matches && !result.npcs.empty())
            {
                matching_lists.push_back(std::move(result.npcs));
            }
        }
    }

    return merge_weighted_lists(matching_lists);
}

auto spawn_rule_engine::get_map_rule_count(std::string_view map_name) const -> size_t
{
    auto it = map_rule_indices_.find(std::string(map_name));
    if (it != map_rule_indices_.end())
    {
        return it->second.size();
    }
    return global_rules_.size(); // Use all rules if no map-specific config
}

auto spawn_rule_engine::merge_weighted_lists(const std::vector<std::vector<weighted_npc_entry>>& lists) const
    -> std::vector<weighted_npc_entry>
{
    if (lists.empty())
    {
        return {};
    }

    if (lists.size() == 1)
    {
        return lists[0];
    }

    // Merge lists, summing weights for duplicate NPCs
    std::unordered_map<npc_id, weighted_npc_entry> merged;

    for (const auto& list : lists)
    {
        for (const auto& entry : list)
        {
            auto it = merged.find(entry.id);
            if (it != merged.end())
            {
                // Sum weights for duplicate NPCs
                it->second.weight += entry.weight;
            }
            else
            {
                merged[entry.id] = entry;
            }
        }
    }

    // Convert map to vector
    std::vector<weighted_npc_entry> result;
    result.reserve(merged.size());
    for (const auto& [id, entry] : merged)
    {
        result.push_back(entry);
    }

    return result;
}

auto spawn_rule_engine::select_from_weighted(const std::vector<weighted_npc_entry>& entries) const
    -> std::optional<npc_id>
{
    if (entries.empty())
    {
        return std::nullopt;
    }

    // Build weight vector
    std::vector<int> weights;
    weights.reserve(entries.size());
    for (const auto& entry : entries)
    {
        weights.push_back(entry.weight);
    }

    // Use discrete distribution for weighted selection
    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
    size_t selected = dist(rng_);

    return entries[selected].id;
}

// Helper function to parse NPC list from YAML
// If whitelist is provided, only NPCs in the whitelist are included
static auto parse_npc_list(const YAML::Node& npcs_node,
                           npc_registry* registry,
                           const random_spawn_whitelist* whitelist = nullptr) -> std::vector<weighted_npc_entry>
{
    std::vector<weighted_npc_entry> npcs;

    if (!npcs_node.IsSequence())
    {
        return npcs;
    }

    for (const auto& npc_node : npcs_node)
    {
        std::string name = npc_node["name"].as<std::string>();
        int weight = npc_node["weight"].as<int>(1);

        // Check whitelist if provided
        if (whitelist && whitelist->spawnable_count() > 0)
        {
            if (!whitelist->is_allowed(name))
            {
                LOG_DEBUG(general, "NPC '{}' not in random spawn whitelist, skipping", name);
                continue;
            }
        }

        // Look up NPC from registry
        auto* tmpl = registry->find_by_name(name);
        if (!tmpl)
        {
            LOG_WARN(general, "Unknown NPC name in spawn table: {}", name);
            continue;
        }

        npcs.push_back(weighted_npc_entry{tmpl->id, name, weight});
    }

    return npcs;
}

// Helper function to parse biome type from string
static auto parse_biome_type(const std::string& str) -> biome_type
{
    if (str == "water")
        return biome_type::water;
    if (str == "farm")
        return biome_type::farm;
    if (str == "forest")
        return biome_type::forest;
    if (str == "mountain")
        return biome_type::mountain;
    if (str == "dungeon")
        return biome_type::dungeon;
    if (str == "safe_zone")
        return biome_type::safe_zone;
    if (str == "fight_zone")
        return biome_type::fight_zone;
    return biome_type::forest; // Default
}

// Helper function to parse time period from string
static auto parse_time_period(const std::string& str) -> time_period
{
    if (str == "day")
        return time_period::day;
    if (str == "night")
        return time_period::night;
    if (str == "dawn")
        return time_period::dawn;
    if (str == "dusk")
        return time_period::dusk;
    if (str == "hour_range")
        return time_period::hour_range;
    return time_period::day; // Default
}

// Helper function to parse proximity condition from string
static auto parse_proximity_condition(const std::string& str) -> proximity_condition
{
    if (str == "min_distance")
        return proximity_condition::min_distance;
    if (str == "max_distance")
        return proximity_condition::max_distance;
    if (str == "player_count")
        return proximity_condition::player_count;
    return proximity_condition::min_distance; // Default
}

// Helper function to parse composite operator from string
static auto parse_composite_operator(const std::string& str) -> composite_operator
{
    if (str == "and")
        return composite_operator::op_and;
    if (str == "or")
        return composite_operator::op_or;
    if (str == "not")
        return composite_operator::op_not;
    return composite_operator::op_and; // Default
}

// Helper function to parse weather type from string
static auto parse_weather_type(const std::string& str) -> world::weather_type
{
    if (str == "clear")
        return world::weather_type::clear;
    if (str == "rain")
        return world::weather_type::rain;
    if (str == "snow")
        return world::weather_type::snow;
    return world::weather_type::clear; // Default
}

// Forward declaration for recursive parsing
static auto parse_rule(const YAML::Node& node,
                       npc_registry* registry,
                       const random_spawn_whitelist* whitelist) -> std::unique_ptr<spawn_rule>;

// Parse a single spawn rule from YAML
static auto parse_rule(const YAML::Node& node,
                       npc_registry* registry,
                       const random_spawn_whitelist* whitelist) -> std::unique_ptr<spawn_rule>
{
    if (!node["type"])
    {
        return nullptr;
    }

    std::string type = node["type"].as<std::string>();
    std::string name = node["name"] ? node["name"].as<std::string>() : "unnamed";

    // Parse NPC list (all rules have this), filtering by whitelist
    auto npcs = parse_npc_list(node["npcs"], registry, whitelist);

    if (type == "level")
    {
        int level = node["level"].as<int>(1);
        return std::make_unique<level_rule>(name, level, std::move(npcs));
    }
    else if (type == "biome")
    {
        auto biome = parse_biome_type(node["biome"].as<std::string>());
        return std::make_unique<biome_rule>(name, biome, std::move(npcs));
    }
    else if (type == "time")
    {
        auto period = parse_time_period(node["period"].as<std::string>());
        int start_hour = node["start_hour"] ? node["start_hour"].as<int>() : 0;
        int end_hour = node["end_hour"] ? node["end_hour"].as<int>() : 0;
        return std::make_unique<time_rule>(name, period, std::move(npcs), start_hour, end_hour);
    }
    else if (type == "weather")
    {
        auto weather = parse_weather_type(node["weather"].as<std::string>());
        return std::make_unique<weather_rule>(name, weather, std::move(npcs));
    }
    else if (type == "proximity")
    {
        auto condition = parse_proximity_condition(node["condition"].as<std::string>());
        int threshold = node["threshold"].as<int>(10);
        int range = node["range"] ? node["range"].as<int>() : 20;
        return std::make_unique<proximity_rule>(name, condition, threshold, std::move(npcs), range);
    }
    else if (type == "event")
    {
        std::string event = node["event"].as<std::string>();
        return std::make_unique<event_rule>(name, event, std::move(npcs));
    }
    else if (type == "composite")
    {
        auto op = parse_composite_operator(node["operator"].as<std::string>());

        std::vector<std::unique_ptr<spawn_rule>> children;
        if (node["children"] && node["children"].IsSequence())
        {
            for (const auto& child_node : node["children"])
            {
                auto child = parse_rule(child_node, registry, whitelist);
                if (child)
                {
                    children.push_back(std::move(child));
                }
            }
        }

        return std::make_unique<composite_rule>(name, op, std::move(children), std::move(npcs));
    }

    return nullptr;
}

} // namespace hb::npc
