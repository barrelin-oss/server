#pragma once

#include "spawn_context.h"
#include "core/types.h"
#include "core/result.h"
#include <vector>
#include <string>
#include <memory>

namespace hb::npc
{

// Weighted NPC entry for spawn selection
struct weighted_npc_entry
{
    npc_id id;
    std::string name;
    int weight;
};

// Result of rule evaluation
struct rule_result
{
    bool matches;
    std::vector<weighted_npc_entry> npcs;

    static auto pass(std::vector<weighted_npc_entry> npcs) -> rule_result { return rule_result{true, std::move(npcs)}; }

    static auto fail() -> rule_result { return rule_result{false, {}}; }
};

// Base interface for all spawn rules
class spawn_rule
{
public:
    virtual ~spawn_rule() = default;

    // Evaluate this rule against the given context
    virtual auto evaluate(const spawn_context& ctx) const -> rule_result = 0;

    // Get rule name (for debugging)
    virtual auto name() const -> std::string_view = 0;
};

// Level-based rule (backward compatible with old system)
class level_rule : public spawn_rule
{
public:
    level_rule(std::string name, int level, std::vector<weighted_npc_entry> npcs)
        : name_(std::move(name))
        , level_(level)
        , npcs_(std::move(npcs))
    {
    }

    auto evaluate(const spawn_context& ctx) const -> rule_result override;
    auto name() const -> std::string_view override { return name_; }

private:
    std::string name_;
    int level_;
    std::vector<weighted_npc_entry> npcs_;
};

// Biome-based rule
enum class biome_type
{
    water,
    farm,
    forest,
    mountain,
    dungeon,
    safe_zone,
    fight_zone
};

class biome_rule : public spawn_rule
{
public:
    biome_rule(std::string name, biome_type biome, std::vector<weighted_npc_entry> npcs)
        : name_(std::move(name))
        , biome_(biome)
        , npcs_(std::move(npcs))
    {
    }

    auto evaluate(const spawn_context& ctx) const -> rule_result override;
    auto name() const -> std::string_view override { return name_; }

private:
    std::string name_;
    biome_type biome_;
    std::vector<weighted_npc_entry> npcs_;
};

// Time-based rule
enum class time_period
{
    day,
    night,
    dawn,
    dusk,
    hour_range
};

class time_rule : public spawn_rule
{
public:
    time_rule(std::string name,
              time_period period,
              std::vector<weighted_npc_entry> npcs,
              int start_hour = 0,
              int end_hour = 0)
        : name_(std::move(name))
        , period_(period)
        , start_hour_(start_hour)
        , end_hour_(end_hour)
        , npcs_(std::move(npcs))
    {
    }

    auto evaluate(const spawn_context& ctx) const -> rule_result override;
    auto name() const -> std::string_view override { return name_; }

private:
    std::string name_;
    time_period period_;
    int start_hour_;
    int end_hour_;
    std::vector<weighted_npc_entry> npcs_;
};

// Weather-based rule
class weather_rule : public spawn_rule
{
public:
    weather_rule(std::string name, world::weather_type weather, std::vector<weighted_npc_entry> npcs)
        : name_(std::move(name))
        , weather_(weather)
        , npcs_(std::move(npcs))
    {
    }

    auto evaluate(const spawn_context& ctx) const -> rule_result override;
    auto name() const -> std::string_view override { return name_; }

private:
    std::string name_;
    world::weather_type weather_;
    std::vector<weighted_npc_entry> npcs_;
};

// Proximity-based rule
enum class proximity_condition
{
    min_distance, // Nearest player must be at least N tiles away
    max_distance, // Nearest player must be at most N tiles away
    player_count  // At least N players within range
};

class proximity_rule : public spawn_rule
{
public:
    proximity_rule(std::string name,
                   proximity_condition condition,
                   int threshold,
                   std::vector<weighted_npc_entry> npcs,
                   int range = 20)
        : name_(std::move(name))
        , condition_(condition)
        , threshold_(threshold)
        , range_(range)
        , npcs_(std::move(npcs))
    {
    }

    auto evaluate(const spawn_context& ctx) const -> rule_result override;
    auto name() const -> std::string_view override { return name_; }

private:
    std::string name_;
    proximity_condition condition_;
    int threshold_;
    int range_; // Only used for player_count condition
    std::vector<weighted_npc_entry> npcs_;
};

// Event-based rule
class event_rule : public spawn_rule
{
public:
    event_rule(std::string name, std::string event, std::vector<weighted_npc_entry> npcs)
        : name_(std::move(name))
        , event_(std::move(event))
        , npcs_(std::move(npcs))
    {
    }

    auto evaluate(const spawn_context& ctx) const -> rule_result override;
    auto name() const -> std::string_view override { return name_; }

private:
    std::string name_;
    std::string event_;
    std::vector<weighted_npc_entry> npcs_;
};

// Composite rule (AND/OR/NOT)
enum class composite_operator
{
    op_and,
    op_or,
    op_not
};

class composite_rule : public spawn_rule
{
public:
    composite_rule(std::string name,
                   composite_operator op,
                   std::vector<std::unique_ptr<spawn_rule>> children,
                   std::vector<weighted_npc_entry> npcs)
        : name_(std::move(name))
        , op_(op)
        , children_(std::move(children))
        , npcs_(std::move(npcs))
    {
    }

    auto evaluate(const spawn_context& ctx) const -> rule_result override;
    auto name() const -> std::string_view override { return name_; }

private:
    std::string name_;
    composite_operator op_;
    std::vector<std::unique_ptr<spawn_rule>> children_;
    std::vector<weighted_npc_entry> npcs_;
};

} // namespace hb::npc
