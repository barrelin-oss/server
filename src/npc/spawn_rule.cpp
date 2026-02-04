#include "spawn_rule.h"

namespace hb::npc
{

// Level rule - backward compatible with old level-based spawning
auto level_rule::evaluate(const spawn_context& ctx) const -> rule_result
{
    // For now, always match (level filtering happens at map level)
    // In future, could query player levels in area
    return rule_result::pass(npcs_);
}

// Biome rule - terrain-based spawning
auto biome_rule::evaluate(const spawn_context& ctx) const -> rule_result
{
    bool matches = false;

    switch (biome_)
    {
        case biome_type::water:
            matches = ctx.is_water();
            break;
        case biome_type::farm:
            matches = ctx.is_farm();
            break;
        case biome_type::forest:
            // TODO: Add forest detection when tile types available
            matches = false;
            break;
        case biome_type::mountain:
            // TODO: Add mountain detection when tile types available
            matches = false;
            break;
        case biome_type::dungeon:
            matches = ctx.is_fight_zone();
            break;
        case biome_type::safe_zone:
            matches = ctx.is_safe_zone();
            break;
        case biome_type::fight_zone:
            matches = ctx.is_fight_zone();
            break;
    }

    return matches ? rule_result::pass(npcs_) : rule_result::fail();
}

// Time rule - time-based spawning
auto time_rule::evaluate(const spawn_context& ctx) const -> rule_result
{
    bool matches = false;

    switch (period_)
    {
        case time_period::day:
            matches = ctx.is_day();
            break;
        case time_period::night:
            matches = ctx.is_night();
            break;
        case time_period::dawn:
            matches = ctx.is_dawn();
            break;
        case time_period::dusk:
            matches = ctx.is_dusk();
            break;
        case time_period::hour_range:
        {
            int hour = ctx.get_hour();
            if (start_hour_ <= end_hour_)
            {
                matches = hour >= start_hour_ && hour < end_hour_;
            }
            else
            {
                // Wraps around midnight (e.g., 23:00 to 01:00)
                matches = hour >= start_hour_ || hour < end_hour_;
            }
            break;
        }
    }

    return matches ? rule_result::pass(npcs_) : rule_result::fail();
}

// Weather rule - weather-based spawning
auto weather_rule::evaluate(const spawn_context& ctx) const -> rule_result
{
    bool matches = (ctx.weather == weather_);
    return matches ? rule_result::pass(npcs_) : rule_result::fail();
}

// Proximity rule - player distance-based spawning
auto proximity_rule::evaluate(const spawn_context& ctx) const -> rule_result
{
    bool matches = false;

    switch (condition_)
    {
        case proximity_condition::min_distance:
        {
            int dist = ctx.get_nearest_player_distance();
            matches = dist >= threshold_;
            break;
        }
        case proximity_condition::max_distance:
        {
            int dist = ctx.get_nearest_player_distance();
            matches = dist <= threshold_;
            break;
        }
        case proximity_condition::player_count:
        {
            int count = ctx.get_player_count_in_range(range_);
            matches = count >= threshold_;
            break;
        }
    }

    return matches ? rule_result::pass(npcs_) : rule_result::fail();
}

// Event rule - event-based spawning
auto event_rule::evaluate(const spawn_context& ctx) const -> rule_result
{
    bool matches = (ctx.active_event == event_);
    return matches ? rule_result::pass(npcs_) : rule_result::fail();
}

// Composite rule - combines multiple rules
auto composite_rule::evaluate(const spawn_context& ctx) const -> rule_result
{
    switch (op_)
    {
        case composite_operator::op_and:
        {
            // All children must match
            for (const auto& child : children_)
            {
                auto result = child->evaluate(ctx);
                if (!result.matches)
                {
                    return rule_result::fail();
                }
            }
            return rule_result::pass(npcs_);
        }

        case composite_operator::op_or:
        {
            // At least one child must match
            for (const auto& child : children_)
            {
                auto result = child->evaluate(ctx);
                if (result.matches)
                {
                    return rule_result::pass(npcs_);
                }
            }
            return rule_result::fail();
        }

        case composite_operator::op_not:
        {
            // First child must NOT match (only use first child for NOT)
            if (children_.empty())
            {
                return rule_result::fail();
            }

            auto result = children_[0]->evaluate(ctx);
            return !result.matches ? rule_result::pass(npcs_) : rule_result::fail();
        }
    }

    return rule_result::fail();
}

} // namespace hb::npc
