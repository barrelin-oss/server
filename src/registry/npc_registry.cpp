// npc_registry.cpp
// NPC registry implementation

#include "registry/npc_registry.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace hb {

namespace {

auto trim(std::string_view str) -> std::string {
    auto start = str.find_first_not_of(" \t\n\r");
    if (start == std::string_view::npos) return "";
    auto end = str.find_last_not_of(" \t\n\r");
    return std::string(str.substr(start, end - start + 1));
}

auto to_lower(std::string str) -> std::string {
    std::transform(str.begin(), str.end(), str.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return str;
}

auto parse_int(std::string_view str, int default_val = 0) -> int {
    auto trimmed = trim(str);
    if (trimmed.empty()) return default_val;
    try {
        return std::stoi(std::string(trimmed));
    } catch (...) {
        return default_val;
    }
}

auto split(std::string_view str, char delim) -> std::vector<std::string> {
    std::vector<std::string> result;
    std::string current;

    for (char c : str) {
        if (c == delim) {
            result.push_back(trim(current));
            current.clear();
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        result.push_back(trim(current));
    }

    return result;
}

}  // namespace

npc_registry::npc_registry() = default;
npc_registry::~npc_registry() = default;

void npc_registry::initialize() {
    LOG_INFO(npc, "NPC registry initialized");
    set_initialized(true);
}

void npc_registry::shutdown() {
    LOG_INFO(npc, "NPC registry shut down ({} NPCs)", npcs_.size());
    npcs_.clear();
    id_index_.clear();
    name_index_.clear();
    set_initialized(false);
}

auto npc_registry::load_from_file(const std::filesystem::path& path)
    -> result<size_t, std::string>
{
    // Auto-detect format based on extension
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return std::tolower(c); });

    if (ext == ".yaml" || ext == ".yml") {
        return load_from_yaml(path);
    }

    // Legacy text format
    std::ifstream file(path);
    if (!file.is_open()) {
        return result<size_t, std::string>::err(
            "Failed to open NPC config: " + path.string()
        );
    }

    LOG_INFO(npc, "Loading NPCs from: {}", path.string());

    std::string line;
    int line_num = 0;
    size_t loaded = 0;
    size_t errors = 0;

    while (std::getline(file, line)) {
        ++line_num;
        line = trim(line);

        // Skip empty lines and comments
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '/') {
            continue;
        }

        auto result = parse_npc_line(line, line_num);
        if (result.is_err()) {
            LOG_WARN(npc, "Line {}: {}", line_num, result.error());
            ++errors;
            continue;
        }

        auto npc = std::move(result.value());

        if (id_index_.contains(npc.id.value)) {
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

auto npc_registry::load_from_yaml(const std::filesystem::path& path)
    -> result<size_t, std::string>
{
    LOG_INFO(npc, "Loading NPCs from YAML: {}", path.string());

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        return result<size_t, std::string>::err(
            "Failed to parse YAML: " + std::string(e.what())
        );
    }

    if (!root["npcs"] || !root["npcs"].IsSequence()) {
        return result<size_t, std::string>::err("Missing or invalid 'npcs' array in YAML");
    }

    size_t loaded = 0;
    size_t errors = 0;
    uint16_t next_id = 1;

    for (const auto& node : root["npcs"]) {
        npc_template npc;

        // Assign sequential ID
        npc.id = npc_id{next_id++};

        // Parse name (required)
        if (!node["name"]) {
            LOG_WARN(npc, "NPC entry {} missing 'name'", loaded + errors + 1);
            ++errors;
            continue;
        }
        npc.name = node["name"].as<std::string>();

        // Parse sprite_id -> sprite + numeric sprite_id
        if (node["sprite_id"]) {
            auto sid = node["sprite_id"].as<int>();
            npc.sprite = std::to_string(sid);
            npc.sprite_id = static_cast<int16_t>(sid);
        }

        // Parse HP (YAML has base HP values)
        if (node["hp"]) {
            npc.hp = node["hp"].as<int32_t>() * 100;  // Scale up: 2 -> 200, etc.
        }

        // Parse MP
        if (node["mp"]) {
            npc.mp = node["mp"].as<int32_t>() * 100;
        }

        // Parse defense
        if (node["defense"]) {
            npc.defense = static_cast<int16_t>(node["defense"].as<int>());
        }

        // Parse level
        if (node["level"]) {
            npc.level = static_cast<int16_t>(node["level"].as<int>());
        }

        // Parse exp (use exp_max as exp_reward)
        if (node["exp_max"]) {
            npc.exp_reward = node["exp_max"].as<int32_t>();
        } else if (node["exp_min"]) {
            npc.exp_reward = node["exp_min"].as<int32_t>();
        }

        // Parse gold range
        if (node["gold_min"]) {
            npc.gold_min = node["gold_min"].as<int32_t>();
        }
        if (node["gold_max"]) {
            npc.gold_max = node["gold_max"].as<int32_t>();
        }

        // Parse drop table
        if (node["drops"] && node["drops"].IsSequence()) {
            for (const auto& drop_node : node["drops"]) {
                npc_drop drop;
                if (drop_node["item_id"]) {
                    drop.item = item_id{static_cast<uint32_t>(drop_node["item_id"].as<int>())};
                }
                if (drop_node["chance"]) {
                    drop.chance = static_cast<int16_t>(drop_node["chance"].as<int>());
                }
                if (drop_node["min_count"]) {
                    drop.count_min = static_cast<int16_t>(drop_node["min_count"].as<int>());
                }
                if (drop_node["max_count"]) {
                    drop.count_max = static_cast<int16_t>(drop_node["max_count"].as<int>());
                }
                if (drop.item.is_valid() && drop.chance > 0) {
                    npc.drops.push_back(drop);
                }
            }
        }

        // Parse attack dice and sides
        if (node["attack_dice"]) {
            npc.attack_dice = static_cast<int16_t>(node["attack_dice"].as<int>());
        }
        if (node["attack_sides"]) {
            npc.attack_sides = static_cast<int16_t>(node["attack_sides"].as<int>());
        }

        // Parse speeds (YAML has ms values, lower = faster)
        if (node["move_speed"]) {
            npc.move_speed = static_cast<int16_t>(node["move_speed"].as<int>());
        }
        if (node["attack_speed"]) {
            npc.attack_speed = static_cast<int16_t>(node["attack_speed"].as<int>());
        }

        // Parse action_time (legacy m_dwActionTime - base AI tick interval in ms)
        if (node["action_time"]) {
            npc.action_time = node["action_time"].as<int32_t>();
        }

        // Parse sight/detection range
        if (node["detection_range"]) {
            npc.sight_range = static_cast<int16_t>(node["detection_range"].as<int>());
            if (npc.sight_range == 0) {
                npc.sight_range = 10;  // Default sight range
            }
        }

        // Parse attack range (YAML stores as *1000, e.g., 5000 = 5 tiles)
        if (node["attack_range"]) {
            int raw_range = node["attack_range"].as<int>();
            npc.attack_range = static_cast<int16_t>(raw_range / 1000);
            if (npc.attack_range < 1) npc.attack_range = 1;
        }

        // Parse is_peaceful -> is_aggressive (inverted, 10 = peaceful, 0 = aggressive)
        if (node["is_peaceful"]) {
            int peaceful = node["is_peaceful"].as<int>();
            npc.is_aggressive = (peaceful < 5);  // 0-4 = aggressive, 5-10 = peaceful
        }

        // Parse is_undead
        if (node["is_undead"]) {
            int undead_value = node["is_undead"].as<int>();
            npc.is_undead = (undead_value > 0 && undead_value != 1);  // 1 seems to mean alive NPCs
        }

        // Parse side (0=neutral, 1=aresden, 2=elvine, 10=hostile to all)
        if (node["side"]) {
            int side = node["side"].as<int>();
            // Side 10 means hostile monster, 0 = neutral NPC, 1/2 = faction-specific
            if (side == 10) {
                npc.type = npc_type::monster;
            } else if (side == 0) {
                // Check if it's actually a town NPC (detection_range == 1 is a hint)
                if (node["detection_range"] && node["detection_range"].as<int>() == 1) {
                    npc.type = npc_type::npc;
                    npc.is_aggressive = false;
                }
            }
        }

        // Parse magic stats
        if (node["magic_level"]) {
            npc.magic_resist = static_cast<int16_t>(node["magic_level"].as<int>());
        }

        // Check for duplicate ID
        if (id_index_.contains(npc.id.value)) {
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

auto npc_registry::parse_npc_line(std::string_view line, int line_num)
    -> result<npc_template, std::string>
{
    // Expected format:
    // ID  Name  Type  HP  MP  Level  Attack  Defense  Exp  GoldMin  GoldMax  ...

    auto parts = split(line, '\t');
    if (parts.size() < 1) {
        parts = split(line, ' ');
    }

    if (parts.size() < 6) {
        return result<npc_template, std::string>::err(
            "Too few fields (need at least 6)"
        );
    }

    npc_template npc;

    // Parse ID
    npc.id = npc_id{static_cast<uint16_t>(parse_int(parts[0]))};
    if (!npc.id.is_valid()) {
        return result<npc_template, std::string>::err("Invalid NPC ID");
    }

    // Parse name
    npc.name = parts[1];
    if (npc.name.empty()) {
        return result<npc_template, std::string>::err("Empty NPC name");
    }

    // Parse type
    npc.type = static_cast<npc_type>(parse_int(parts[2]));

    // Parse HP
    npc.hp = parse_int(parts[3]);

    // Parse MP
    npc.mp = parse_int(parts[4]);

    // Parse Level
    npc.level = static_cast<int16_t>(parse_int(parts[5]));

    // Optional combat stats
    if (parts.size() > 6) npc.attack_dice = static_cast<int16_t>(parse_int(parts[6]));
    if (parts.size() > 7) npc.attack_sides = static_cast<int16_t>(parse_int(parts[7]));
    if (parts.size() > 8) npc.attack_bonus = static_cast<int16_t>(parse_int(parts[8]));
    if (parts.size() > 9) npc.defense = static_cast<int16_t>(parse_int(parts[9]));

    // Rewards
    if (parts.size() > 10) npc.exp_reward = parse_int(parts[10]);
    if (parts.size() > 11) npc.gold_min = parse_int(parts[11]);
    if (parts.size() > 12) npc.gold_max = parse_int(parts[12]);

    // Speed
    if (parts.size() > 13) npc.move_speed = static_cast<int16_t>(parse_int(parts[13]));
    if (parts.size() > 14) npc.attack_speed = static_cast<int16_t>(parse_int(parts[14]));

    return result<npc_template, std::string>::ok(std::move(npc));
}

auto npc_registry::get(npc_id id) const -> const npc_template* {
    auto it = id_index_.find(id.value);
    if (it == id_index_.end()) {
        return nullptr;
    }
    return &npcs_[it->second];
}

auto npc_registry::find_by_name(std::string_view name) const -> const npc_template* {
    auto it = name_index_.find(to_lower(std::string(name)));
    if (it == name_index_.end()) {
        return nullptr;
    }
    return &npcs_[it->second];
}

auto npc_registry::by_type(npc_type type) const -> std::vector<const npc_template*> {
    std::vector<const npc_template*> result;
    for (const auto& npc : npcs_) {
        if (npc.type == type) {
            result.push_back(&npc);
        }
    }
    return result;
}

auto npc_registry::bosses() const -> std::vector<const npc_template*> {
    std::vector<const npc_template*> result;
    for (const auto& npc : npcs_) {
        if (npc.is_boss) {
            result.push_back(&npc);
        }
    }
    return result;
}

auto npc_registry::by_level_range(int min_level, int max_level) const
    -> std::vector<const npc_template*>
{
    std::vector<const npc_template*> result;
    for (const auto& npc : npcs_) {
        if (npc.level >= min_level && npc.level <= max_level) {
            result.push_back(&npc);
        }
    }
    return result;
}

auto npc_registry::count() const -> size_t {
    return npcs_.size();
}

auto npc_registry::exists(npc_id id) const -> bool {
    return id_index_.contains(id.value);
}

auto npc_registry::all() const -> const std::vector<npc_template>& {
    return npcs_;
}

}  // namespace hb
