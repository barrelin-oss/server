// npc_registry.cpp
// NPC registry implementation

#include "registry/npc_registry.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace hb
{

namespace
{

auto trim(std::string_view str) -> std::string
{
    auto start = str.find_first_not_of(" \t\n\r");
    if (start == std::string_view::npos)
        return "";
    auto end = str.find_last_not_of(" \t\n\r");
    return std::string(str.substr(start, end - start + 1));
}

auto to_lower(std::string str) -> std::string
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
}

auto parse_int(std::string_view str, int default_val = 0) -> int
{
    auto trimmed = trim(str);
    if (trimmed.empty())
        return default_val;
    try
    {
        return std::stoi(std::string(trimmed));
    }
    catch (...)
    {
        return default_val;
    }
}

auto split(std::string_view str, char delim) -> std::vector<std::string>
{
    std::vector<std::string> result;
    std::string current;

    for (char c : str)
    {
        if (c == delim)
        {
            result.push_back(trim(current));
            current.clear();
        }
        else
        {
            current += c;
        }
    }

    if (!current.empty())
    {
        result.push_back(trim(current));
    }

    return result;
}

} // namespace

npc_registry::npc_registry() = default;
npc_registry::~npc_registry() = default;

void npc_registry::initialize()
{
    LOG_INFO(npc, "NPC registry initialized");
    set_initialized(true);
}

void npc_registry::shutdown()
{
    LOG_INFO(npc, "NPC registry shut down ({} NPCs)", npcs_.size());
    npcs_.clear();
    id_index_.clear();
    name_index_.clear();
    set_initialized(false);
}

auto npc_registry::load_from_file(const std::filesystem::path& path) -> result<size_t, std::string>
{
    // Auto-detect format based on extension
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });

    if (ext == ".yaml" || ext == ".yml")
    {
        return load_from_yaml(path);
    }

    // Legacy text format
    std::ifstream file(path);
    if (!file.is_open())
    {
        return result<size_t, std::string>::err("Failed to open NPC config: " + path.string());
    }

    LOG_INFO(npc, "Loading NPCs from: {}", path.string());

    std::string line;
    int line_num = 0;
    size_t loaded = 0;
    size_t errors = 0;

    while (std::getline(file, line))
    {
        ++line_num;
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '/')
        {
            continue;
        }

        auto result = parse_npc_line(line, line_num);
        if (result.is_err())
        {
            LOG_WARN(npc, "Line {}: {}", line_num, result.error());
            ++errors;
            continue;
        }

        auto npc = std::move(result.value());

        if (id_index_.contains(npc.id.value))
        {
            LOG_WARN(npc, "Line {}: Duplicate NPC ID {}", line_num, npc.id.value);
            ++errors;
            continue;
        }

        auto index = npcs_.size();
        id_index_[npc.id.value] = index;
        name_index_[to_lower(npc.name)] = index;
        npcs_.push_back(std::move(npc));
        ++loaded;
    }

    LOG_INFO(npc, "Loaded {} NPCs ({} errors)", loaded, errors);

    return result<size_t, std::string>::ok(loaded);
}

auto npc_registry::load_from_yaml(const std::filesystem::path& path) -> result<size_t, std::string>
{
    LOG_INFO(npc, "Loading NPCs from YAML: {}", path.string());

    YAML::Node root;
    try
    {
        root = YAML::LoadFile(path.string());
    }
    catch (const YAML::Exception& e)
    {
        return result<size_t, std::string>::err("Failed to parse YAML: " + std::string(e.what()));
    }

    if (!root["npcs"] || !root["npcs"].IsSequence())
    {
        return result<size_t, std::string>::err("Missing or invalid 'npcs' array in YAML");
    }

    size_t loaded = 0;
    size_t errors = 0;
    uint16_t next_id = 1;

    for (const auto& node : root["npcs"])
    {
        npc_template npc;

        // Assign sequential ID
        npc.id = npc_id{next_id++};

        // Parse name (required)
        if (!node["name"])
        {
            LOG_WARN(npc, "NPC entry {} missing 'name'", loaded + errors + 1);
            ++errors;
            continue;
        }
        npc.name = node["name"].as<std::string>();

        // Parse sprite_id (Type column)
        if (node["sprite_id"])
        {
            auto sid = node["sprite_id"].as<int>();
            npc.sprite = std::to_string(sid);
            npc.sprite_id = static_cast<int16_t>(sid);
        }

        // --- Raw cfg fields ---
        if (node["hit_dice"])
            npc.hit_dice = static_cast<int16_t>(node["hit_dice"].as<int>());
        // O npcs.yaml e gerado por tools/convert a partir do .cfg legado. "defense_ratio"
        // ali NAO e absorcao de dano: e o denominador da chance de acerto, que em
        // damage_calc.h vira calc_hit_chance(hit_rate, dodge_rate). Como o registry nunca
        // preenchia dodge_rate, ele ficava 0 -> max(1,0) -> todo golpe acertava no teto
        // de 99%. npc.defense (uma % de absorcao com teto 80) nao tem fonte no formato
        // legado e continua 0 de proposito.
        if (node["defense_ratio"])
            npc.dodge_rate = static_cast<int16_t>(node["defense_ratio"].as<int>());
        if (node["hit_ratio"])
            npc.hit_ratio = static_cast<int16_t>(node["hit_ratio"].as<int>());
        if (node["min_bravery"])
            npc.min_bravery = static_cast<int16_t>(node["min_bravery"].as<int>());
        if (node["exp"])
            npc.exp_reward = node["exp"].as<int32_t>();
        if (node["gold_min"])
            npc.gold_min = node["gold_min"].as<int32_t>();
        if (node["gold_max"])
            npc.gold_max = node["gold_max"].as<int32_t>();
        if (node["attack_dice"])
            npc.attack_dice = static_cast<int16_t>(node["attack_dice"].as<int>());
        if (node["attack_sides"])
            npc.attack_sides = static_cast<int16_t>(node["attack_sides"].as<int>());
        if (node["size"])
            npc.body_size = static_cast<int16_t>(node["size"].as<int>());
        if (node["action_limit"])
            npc.action_limit = static_cast<int16_t>(node["action_limit"].as<int>());
        if (node["action_time"])
            npc.action_time = node["action_time"].as<int32_t>();
        if (node["resist_magic"])
            npc.magic_resist = static_cast<int16_t>(node["resist_magic"].as<int>());
        if (node["magic_level"])
            npc.magic_level = static_cast<int16_t>(node["magic_level"].as<int>());
        if (node["day_of_week"])
            npc.day_of_week = static_cast<int16_t>(node["day_of_week"].as<int>());
        if (node["chat_msg"])
            npc.chat_msg = static_cast<int16_t>(node["chat_msg"].as<int>());
        if (node["regen_time"])
            npc.regen_time = node["regen_time"].as<int32_t>();
        if (node["attribute"])
            npc.attribute = static_cast<int16_t>(node["attribute"].as<int>());
        if (node["abs_damage"])
            npc.abs_damage = static_cast<int16_t>(node["abs_damage"].as<int>());
        if (node["magic_hit_ratio"])
            npc.magic_hit_ratio = static_cast<int16_t>(node["magic_hit_ratio"].as<int>());
        if (node["area"])
            npc.area = static_cast<int16_t>(node["area"].as<int>());

        // Special ability spawn config
        if (node["sa_prob"])
            npc.sa_prob = static_cast<int16_t>(node["sa_prob"].as<int>());
        if (node["sa_pool"])
            npc.sa_pool = static_cast<int16_t>(node["sa_pool"].as<int>());

        // Parse detection_range -> sight_range
        if (node["detection_range"])
        {
            npc.sight_range = static_cast<int16_t>(node["detection_range"].as<int>());
            if (npc.sight_range == 0)
            {
                npc.sight_range = 10; // Default sight range
            }
        }

        // Parse attack_range (now stored as raw tile value, not *1000)
        if (node["attack_range"])
        {
            npc.attack_range = static_cast<int16_t>(node["attack_range"].as<int>());
            if (npc.attack_range < 1)
                npc.attack_range = 1;
        }

        // Parse max_mana -> mp
        if (node["max_mana"])
        {
            npc.mp = node["max_mana"].as<int32_t>();
        }

        // --- Compute derived fields ---
        npc.level = npc.hit_ratio;
        npc.hit_rate = npc.hit_ratio;

        // HP from hit_dice (simple scaling for template; actual spawn uses dice formula)
        // Template HP = rough estimate for level range queries and admin display
        if (npc.hit_dice <= 5)
        {
            npc.hp = npc.hit_dice * 3 + npc.hit_dice; // Approximate average of iDice(hd, 4) + hd
        }
        else
        {
            npc.hp = npc.hit_dice * 5 + npc.hit_dice; // Approximate average of hd*5 + iDice(1, hd)
        }

        // --- Determine NPC type from side + action_limit ---
        int side = 10; // default
        if (node["side"])
        {
            side = node["side"].as<int>();
        }

        // action_limit-based classification
        switch (npc.action_limit)
        {
        case 2: // Town NPC (shopkeeper, trainer, etc.)
            npc.type = npc_type::npc;
            npc.is_aggressive = false;
            npc.can_talk = true;
            break;
        case 6: // Quest NPC
            npc.type = npc_type::npc;
            npc.is_aggressive = false;
            npc.can_talk = true;
            break;
        case 3: // Training dummy - targetable but passive
        case 4: // Energy sphere - targetable but passive
            npc.type = npc_type::monster;
            npc.is_aggressive = false;
            break;
        case 8: // Gate/structure - targetable, passive
            npc.type = npc_type::monster;
            npc.is_aggressive = false;
            break;
        case 5: // War unit - faction-controlled
            npc.type = npc_type::monster;
            // War units with side=10 are aggressive; faction units handled by war system
            npc.is_aggressive = (side == 10);
            break;
        default: // action_limit 0 = normal behavior
            if (side == 10)
            {
                npc.type = npc_type::monster;
                npc.is_aggressive = true;
            }
            else if (side == 1 || side == 2)
            {
                // Faction unit - could be guard or war unit
                // Guards are identified by name prefix "Guard-"
                if (npc.name.starts_with("Guard-"))
                {
                    npc.type = npc_type::guard;
                    npc.is_aggressive = true; // Guards are aggressive to criminals
                }
                else
                {
                    npc.type = npc_type::monster;
                    npc.is_aggressive = true; // Faction war units
                }
            }
            else
            {
                // side == 0 - neutral NPC or passive creature
                npc.type = npc_type::npc;
                npc.is_aggressive = false;
            }
            break;
        }

        // --- Determine is_undead from name ---
        npc.is_undead = (npc.name == "Zombie" || npc.name == "Skeleton" || npc.name == "Liche" || npc.name == "Ghost" ||
                         npc.name == "Vampire");

        // Parse drop table (if present in YAML - not from cfg)
        if (node["drops"] && node["drops"].IsSequence())
        {
            for (const auto& drop_node : node["drops"])
            {
                npc_drop drop;
                if (drop_node["item_id"])
                {
                    drop.item = item_id{static_cast<uint32_t>(drop_node["item_id"].as<int>())};
                }
                if (drop_node["chance"])
                {
                    drop.chance = static_cast<int16_t>(drop_node["chance"].as<int>());
                }
                if (drop_node["min_count"])
                {
                    drop.count_min = static_cast<int16_t>(drop_node["min_count"].as<int>());
                }
                if (drop_node["max_count"])
                {
                    drop.count_max = static_cast<int16_t>(drop_node["max_count"].as<int>());
                }
                if (drop.item.is_valid() && drop.chance > 0)
                {
                    npc.drops.push_back(drop);
                }
            }
        }

        // Check for duplicate ID
        if (id_index_.contains(npc.id.value))
        {
            LOG_WARN(npc, "Duplicate NPC ID {}", npc.id.value);
            ++errors;
            continue;
        }

        auto index = npcs_.size();
        id_index_[npc.id.value] = index;
        name_index_[to_lower(npc.name)] = index;
        npcs_.push_back(std::move(npc));
        ++loaded;
    }

    LOG_INFO(npc, "Loaded {} NPCs from YAML ({} errors)", loaded, errors);

    return result<size_t, std::string>::ok(loaded);
}

auto npc_registry::parse_npc_line(std::string_view line, [[maybe_unused]] int line_num) -> result<npc_template, std::string>
{
    // Expected format:
    // ID  Name  Type  HP  MP  Level  Attack  Defense  Exp  GoldMin  GoldMax  ...

    auto parts = split(line, '\t');
    if (parts.size() < 1)
    {
        parts = split(line, ' ');
    }

    if (parts.size() < 6)
    {
        return result<npc_template, std::string>::err("Too few fields (need at least 6)");
    }

    npc_template npc;

    // Parse ID
    npc.id = npc_id{static_cast<uint16_t>(parse_int(parts[0]))};
    if (!npc.id.is_valid())
    {
        return result<npc_template, std::string>::err("Invalid NPC ID");
    }

    // Parse name
    npc.name = parts[1];
    if (npc.name.empty())
    {
        return result<npc_template, std::string>::err("Empty NPC name");
    }

    // Parse type
    {
        auto type_val = parse_int(parts[2]);
        if (type_val < 0 || type_val > 6)
        {
            LOG_WARN(npc, "NPC {}: invalid type {}, defaulting to monster", npc.name, type_val);
            npc.type = npc_type::monster;
        }
        else
        {
            npc.type = static_cast<npc_type>(type_val);
        }
    }

    // Parse HP
    npc.hp = parse_int(parts[3]);

    // Parse MP
    npc.mp = parse_int(parts[4]);

    // Parse Level
    npc.level = static_cast<int16_t>(parse_int(parts[5]));

    // Optional combat stats
    if (parts.size() > 6)
        npc.attack_dice = static_cast<int16_t>(parse_int(parts[6]));
    if (parts.size() > 7)
        npc.attack_sides = static_cast<int16_t>(parse_int(parts[7]));
    if (parts.size() > 8)
        npc.attack_bonus = static_cast<int16_t>(parse_int(parts[8]));
    if (parts.size() > 9)
        npc.defense = static_cast<int16_t>(parse_int(parts[9]));

    // Rewards
    if (parts.size() > 10)
        npc.exp_reward = parse_int(parts[10]);
    if (parts.size() > 11)
        npc.gold_min = parse_int(parts[11]);
    if (parts.size() > 12)
        npc.gold_max = parse_int(parts[12]);

    return result<npc_template, std::string>::ok(std::move(npc));
}

auto npc_registry::get(npc_id id) const -> const npc_template*
{
    auto it = id_index_.find(id.value);
    if (it == id_index_.end())
    {
        return nullptr;
    }
    return &npcs_[it->second];
}

auto npc_registry::find_by_name(std::string_view name) const -> const npc_template*
{
    auto it = name_index_.find(to_lower(std::string(name)));
    if (it == name_index_.end())
    {
        return nullptr;
    }
    return &npcs_[it->second];
}

auto npc_registry::by_type(npc_type type) const -> std::vector<const npc_template*>
{
    std::vector<const npc_template*> result;
    for (const auto& npc : npcs_)
    {
        if (npc.type == type)
        {
            result.push_back(&npc);
        }
    }
    return result;
}

auto npc_registry::bosses() const -> std::vector<const npc_template*>
{
    std::vector<const npc_template*> result;
    for (const auto& npc : npcs_)
    {
        if (npc.is_boss)
        {
            result.push_back(&npc);
        }
    }
    return result;
}

auto npc_registry::by_level_range(int min_level, int max_level) const -> std::vector<const npc_template*>
{
    std::vector<const npc_template*> result;
    for (const auto& npc : npcs_)
    {
        if (npc.level >= min_level && npc.level <= max_level)
        {
            result.push_back(&npc);
        }
    }
    return result;
}

auto npc_registry::count() const -> size_t
{
    return npcs_.size();
}

auto npc_registry::exists(npc_id id) const -> bool
{
    return id_index_.contains(id.value);
}

auto npc_registry::all() const -> const std::vector<npc_template>&
{
    return npcs_;
}

auto npc_registry::load_sa_config(const std::filesystem::path& path) -> result<size_t, std::string>
{
    auto res = npc::load_sa_config(path);
    if (res.is_err())
        return result<size_t, std::string>::err(res.error());

    sa_config_ = std::move(res.value());
    return result<size_t, std::string>::ok(sa_config_.pools.size());
}

} // namespace hb
