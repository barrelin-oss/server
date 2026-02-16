#pragma once

// special_ability.h
// NPC special ability pool configuration and roll/apply logic

#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <filesystem>

#include "core/result.h"

namespace hb::npc
{

// Forward declaration — full definition in npc.h
struct npc;

// A single ability definition (from config)
struct sa_ability_def
{
    int8_t id{0};
    std::string name;
};

// A pool of abilities that can be rolled from
struct sa_pool_def
{
    int16_t id{0};
    std::vector<int8_t> abilities;
};

// Full SA configuration loaded from special_abilities.yaml
struct sa_config
{
    std::vector<sa_ability_def> abilities;
    std::unordered_map<int16_t, sa_pool_def> pools;

    // Look up a pool by ID. Returns nullptr if not found.
    [[nodiscard]] auto get_pool(int16_t pool_id) const -> const sa_pool_def*
    {
        auto it = pools.find(pool_id);
        return it != pools.end() ? &it->second : nullptr;
    }

    // Look up ability name by ID. Returns empty if not found.
    [[nodiscard]] auto get_ability_name(int8_t sa_id) const -> std::string_view
    {
        for (const auto& a : abilities)
        {
            if (a.id == sa_id)
                return a.name;
        }
        return {};
    }
};

// Load SA configuration from YAML file.
auto load_sa_config(const std::filesystem::path& path) -> result<sa_config, std::string>;

// Roll a random special ability from the given pool.
// Returns 0 if pool not found or empty.
[[nodiscard]] inline auto roll_from_pool(const sa_config& config, int16_t pool_id) -> int8_t
{
    const auto* pool = config.get_pool(pool_id);
    if (!pool || pool->abilities.empty())
        return 0;

    thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<size_t> dist(0, pool->abilities.size() - 1);
    return pool->abilities[dist(rng)];
}

// Apply special ability stat modifications to an NPC instance.
// Handles Beholder override, mutual exclusion, exp bonuses, etc.
void apply_special_ability(npc& n, int8_t sa);

} // namespace hb::npc
