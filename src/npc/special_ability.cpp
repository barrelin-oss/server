// special_ability.cpp
// NPC special ability config loading and stat modification logic

#include "npc/special_ability.h"
#include "npc/npc.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <random>

namespace hb::npc
{

auto load_sa_config(const std::filesystem::path& path) -> result<sa_config, std::string>
{
    if (!std::filesystem::exists(path))
        return result<sa_config, std::string>::err("File not found: " + path.string());

    YAML::Node root;
    try
    {
        root = YAML::LoadFile(path.string());
    }
    catch (const YAML::Exception& e)
    {
        return result<sa_config, std::string>::err("Failed to parse YAML: " + std::string(e.what()));
    }

    sa_config config;

    // Parse abilities
    if (root["abilities"] && root["abilities"].IsSequence())
    {
        for (const auto& node : root["abilities"])
        {
            sa_ability_def def;
            if (node["id"])
                def.id = static_cast<int8_t>(node["id"].as<int>());
            if (node["name"])
                def.name = node["name"].as<std::string>();
            config.abilities.push_back(std::move(def));
        }
    }

    // Parse pools
    if (root["pools"] && root["pools"].IsSequence())
    {
        for (const auto& node : root["pools"])
        {
            sa_pool_def pool;
            if (node["id"])
                pool.id = static_cast<int16_t>(node["id"].as<int>());
            if (node["abilities"] && node["abilities"].IsSequence())
            {
                for (const auto& a : node["abilities"])
                {
                    pool.abilities.push_back(static_cast<int8_t>(a.as<int>()));
                }
            }
            config.pools[pool.id] = std::move(pool);
        }
    }

    LOG_INFO(npc, "Loaded SA config: {} abilities, {} pools", config.abilities.size(), config.pools.size());
    return result<sa_config, std::string>::ok(std::move(config));
}

void apply_special_ability(npc& n, int8_t sa)
{
    // Beholder (sprite_id 53) always gets Clairvoyant
    if (n.sprite_id == 53)
        sa = 1;

    thread_local std::mt19937 rng{std::random_device{}()};

    switch (sa)
    {
    case 1: // Clairvoyant
        n.exp_reward += n.exp_reward / 4; // +25%
        break;

    case 2: // Destructive Magic Protection
        n.exp_reward += n.exp_reward * 30 / 100; // +30%
        break;

    case 3: // Anti-Physical
    {
        if (n.abs_damage > 0)
        {
            // Mutual exclusion: can't have Anti-Physical with positive abs_damage
            sa = 0;
            break;
        }
        std::uniform_int_distribution<int> dist(1, 60);
        int amount = 20 + dist(rng);
        n.abs_damage -= static_cast<int16_t>(amount);
        if (n.abs_damage < -90)
            n.abs_damage = -90;
        // Exp bonus proportional to absorption
        n.exp_reward += static_cast<int32_t>(
            static_cast<double>(n.exp_reward) * static_cast<double>(std::abs(n.abs_damage)) / 100.0);
        break;
    }

    case 4: // Anti-Magic
    {
        if (n.abs_damage < 0)
        {
            // Mutual exclusion: can't have Anti-Magic with negative abs_damage
            sa = 0;
            break;
        }
        std::uniform_int_distribution<int> dist(1, 60);
        int amount = 20 + dist(rng);
        n.abs_damage += static_cast<int16_t>(amount);
        if (n.abs_damage > 90)
            n.abs_damage = 90;
        // Exp bonus proportional to absorption
        n.exp_reward += static_cast<int32_t>(
            static_cast<double>(n.exp_reward) * static_cast<double>(n.abs_damage) / 100.0);
        break;
    }

    case 5: // Poisonous
        n.exp_reward += n.exp_reward * 15 / 100; // +15%
        break;

    case 6: // Critical Poisonous
        // No exp bonus in legacy
        break;

    case 7: // Explosive
        n.exp_reward += n.exp_reward / 5; // +20%
        break;

    case 8: // Hi-Explosive
        n.exp_reward += n.exp_reward / 4; // +25%
        break;

    default:
        sa = 0;
        break;
    }

    n.special_ability = sa;
}

} // namespace hb::npc
