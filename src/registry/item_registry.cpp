// item_registry.cpp
// Item registry implementation

#include "registry/item_registry.h"
#include "core/logger.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <yaml-cpp/yaml.h>

namespace hb {

namespace {

// Trim whitespace
auto trim(std::string_view str) -> std::string {
    auto start = str.find_first_not_of(" \t\n\r");
    if (start == std::string_view::npos) return "";
    auto end = str.find_last_not_of(" \t\n\r");
    return std::string(str.substr(start, end - start + 1));
}

// Convert string to lowercase
auto to_lower(std::string str) -> std::string {
    std::transform(str.begin(), str.end(), str.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return str;
}

// Parse integer with default
auto parse_int(std::string_view str, int default_val = 0) -> int {
    auto trimmed = trim(str);
    if (trimmed.empty()) return default_val;
    try {
        return std::stoi(std::string(trimmed));
    } catch (...) {
        return default_val;
    }
}

// Split string by delimiter
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

item_registry::item_registry() = default;
item_registry::~item_registry() = default;

void item_registry::initialize() {
    LOG_INFO(item, "Item registry initialized");
    set_initialized(true);
}

void item_registry::shutdown() {
    LOG_INFO(item, "Item registry shut down ({} items)", items_.size());
    items_.clear();
    id_index_.clear();
    name_index_.clear();
    set_initialized(false);
}

auto item_registry::load_from_file(const std::filesystem::path& path)
    -> result<size_t, std::string>
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return result<size_t, std::string>::err(
            "Failed to open item config: " + path.string()
        );
    }

    LOG_INFO(item, "Loading items from: {}", path.string());

    // Check if this is a YAML file
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return std::tolower(c); });

    if (ext == ".yaml" || ext == ".yml") {
        return load_from_yaml(path);
    }

    // Legacy CFG format parsing
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

        // Parse item line
        auto result = parse_item_line(line, line_num);
        if (result.is_err()) {
            LOG_WARN(item, "Line {}: {}", line_num, result.error());
            ++errors;
            continue;
        }

        auto item = std::move(result.value());

        // Check for duplicate ID
        if (id_index_.contains(item.id.value)) {
            LOG_WARN(item, "Line {}: Duplicate item ID {}", line_num, item.id.value);
            ++errors;
            continue;
        }

        // Store item
        auto index = items_.size();
        id_index_[item.id.value] = index;
        name_index_[to_lower(item.name)] = index;
        items_.push_back(std::move(item));
        ++loaded;
    }

    LOG_INFO(item, "Loaded {} items ({} errors)", loaded, errors);

    return result<size_t, std::string>::ok(loaded);
}

auto item_registry::load_from_yaml(const std::filesystem::path& path)
    -> result<size_t, std::string>
{
    try {
        YAML::Node root = YAML::LoadFile(path.string());

        if (!root["items"] || !root["items"].IsSequence()) {
            return result<size_t, std::string>::err("YAML must contain 'items' array");
        }

        size_t loaded = 0;
        size_t errors = 0;

        for (const auto& node : root["items"]) {
            item_template item;

            // Required fields
            if (!node["id"]) {
                ++errors;
                continue;
            }

            item.id = item_id{node["id"].as<uint32_t>()};
            if (!item.id.is_valid()) {
                ++errors;
                continue;
            }

            if (node["name"]) {
                item.name = node["name"].as<std::string>();
            } else {
                ++errors;
                continue;
            }

            // Check for duplicate
            if (id_index_.contains(item.id.value)) {
                LOG_WARN(item, "Duplicate item ID {}", item.id.value);
                ++errors;
                continue;
            }

            // Optional fields with defaults
            if (node["type"]) item.type = static_cast<item_type>(node["type"].as<int>());
            if (node["equip_pos"]) item.equip_pos = static_cast<item_equip_pos>(node["equip_pos"].as<int>());
            if (node["weight"]) item.weight = static_cast<int16_t>(node["weight"].as<int>());
            if (node["durability"]) item.max_durability = static_cast<int16_t>(node["durability"].as<int>());
            if (node["price"]) item.price = node["price"].as<int>();
            if (node["level_limit"]) item.level_limit = static_cast<int16_t>(node["level_limit"].as<int>());
            if (node["attack_bonus"]) item.attack_bonus = static_cast<int16_t>(node["attack_bonus"].as<int>());
            if (node["defense"]) item.defense = static_cast<int16_t>(node["defense"].as<int>());
            if (node["hit_prob"]) item.hit_prob_bonus = static_cast<int16_t>(node["hit_prob"].as<int>());
            if (node["dodge_prob"]) item.dodge_prob_bonus = static_cast<int16_t>(node["dodge_prob"].as<int>());
            if (node["is_two_handed"]) item.two_hand_modifier = static_cast<int16_t>(node["is_two_handed"].as<int>());
            if (node["sprite_id"]) item.sprite_id = static_cast<int16_t>(node["sprite_id"].as<int>());

            // Store item
            auto index = items_.size();
            id_index_[item.id.value] = index;
            name_index_[to_lower(item.name)] = index;
            items_.push_back(std::move(item));
            ++loaded;
        }

        LOG_INFO(item, "Loaded {} items from YAML ({} errors)", loaded, errors);
        return result<size_t, std::string>::ok(loaded);

    } catch (const YAML::Exception& e) {
        return result<size_t, std::string>::err(
            std::string("YAML parsing error: ") + e.what()
        );
    }
}

auto item_registry::parse_item_line(std::string_view line, int line_num)
    -> result<item_template, std::string>
{
    // Expected format (tab or space separated):
    // ID  Name  Type  EquipPos  Weight  Price  Attack  Defense  STR  DEX  INT  MAG  ...
    // This is a simplified parser - real format may vary

    auto parts = split(line, '\t');
    if (parts.size() < 1) {
        parts = split(line, ' ');
    }

    if (parts.size() < 5) {
        return result<item_template, std::string>::err(
            "Too few fields (need at least 5)"
        );
    }

    item_template item;

    // Parse ID
    item.id = item_id{static_cast<uint32_t>(parse_int(parts[0]))};
    if (!item.id.is_valid()) {
        return result<item_template, std::string>::err("Invalid item ID");
    }

    // Parse name
    item.name = parts[1];
    if (item.name.empty()) {
        return result<item_template, std::string>::err("Empty item name");
    }

    // Parse type
    item.type = static_cast<item_type>(parse_int(parts[2]));

    // Parse equip position
    item.equip_pos = static_cast<item_equip_pos>(parse_int(parts[3]));

    // Parse weight
    item.weight = static_cast<int16_t>(parse_int(parts[4]));

    // Optional fields (with defaults)
    if (parts.size() > 5) item.price = parse_int(parts[5]);
    if (parts.size() > 6) item.level_limit = static_cast<int16_t>(parse_int(parts[6]));

    // Combat stats
    if (parts.size() > 7) item.attack_dice = static_cast<int16_t>(parse_int(parts[7]));
    if (parts.size() > 8) item.attack_sides = static_cast<int16_t>(parse_int(parts[8]));
    if (parts.size() > 9) item.attack_bonus = static_cast<int16_t>(parse_int(parts[9]));
    if (parts.size() > 10) item.defense = static_cast<int16_t>(parse_int(parts[10]));

    // Requirements
    if (parts.size() > 11) item.str_req = static_cast<int16_t>(parse_int(parts[11]));
    if (parts.size() > 12) item.dex_req = static_cast<int16_t>(parse_int(parts[12]));
    if (parts.size() > 13) item.int_req = static_cast<int16_t>(parse_int(parts[13]));
    if (parts.size() > 14) item.mag_req = static_cast<int16_t>(parse_int(parts[14]));

    // Set derived properties
    item.is_stackable = (item.max_stack > 1);

    return result<item_template, std::string>::ok(std::move(item));
}

auto item_registry::get(item_id id) const -> const item_template* {
    auto it = id_index_.find(id.value);
    if (it == id_index_.end()) {
        return nullptr;
    }
    return &items_[it->second];
}

auto item_registry::find_by_name(std::string_view name) const -> const item_template* {
    auto it = name_index_.find(to_lower(std::string(name)));
    if (it == name_index_.end()) {
        return nullptr;
    }
    return &items_[it->second];
}

auto item_registry::by_type(item_type type) const -> std::vector<const item_template*> {
    std::vector<const item_template*> result;
    for (const auto& item : items_) {
        if (item.type == type) {
            result.push_back(&item);
        }
    }
    return result;
}

auto item_registry::by_category(item_category category) const -> std::vector<const item_template*> {
    std::vector<const item_template*> result;
    for (const auto& item : items_) {
        if (item.category == category) {
            result.push_back(&item);
        }
    }
    return result;
}

auto item_registry::by_equip_pos(item_equip_pos pos) const -> std::vector<const item_template*> {
    std::vector<const item_template*> result;
    for (const auto& item : items_) {
        if (item.equip_pos == pos) {
            result.push_back(&item);
        }
    }
    return result;
}

auto item_registry::count() const -> size_t {
    return items_.size();
}

auto item_registry::exists(item_id id) const -> bool {
    return id_index_.contains(id.value);
}

auto item_registry::all() const -> const std::vector<item_template>& {
    return items_;
}

}  // namespace hb
