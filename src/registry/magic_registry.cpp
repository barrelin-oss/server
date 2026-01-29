// magic_registry.cpp
// Magic registry implementation

#include "registry/magic_registry.h"
#include "core/logger.h"

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

magic_registry::magic_registry() = default;
magic_registry::~magic_registry() = default;

void magic_registry::initialize() {
    LOG_INFO(magic, "Magic registry initialized");
    set_initialized(true);
}

void magic_registry::shutdown() {
    LOG_INFO(magic, "Magic registry shut down ({} spells)", spells_.size());
    spells_.clear();
    id_index_.clear();
    name_index_.clear();
    set_initialized(false);
}

auto magic_registry::load_from_file(const std::filesystem::path& path)
    -> result<size_t, std::string>
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return result<size_t, std::string>::err(
            "Failed to open magic config: " + path.string()
        );
    }

    LOG_INFO(magic, "Loading spells from: {}", path.string());

    std::string line;
    int line_num = 0;
    size_t loaded = 0;
    size_t errors = 0;

    while (std::getline(file, line)) {
        ++line_num;
        line = trim(line);

        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '/') {
            continue;
        }

        auto result = parse_spell_line(line, line_num);
        if (result.is_err()) {
            LOG_WARN(magic, "Line {}: {}", line_num, result.error());
            ++errors;
            continue;
        }

        auto spell = std::move(result.value());

        if (id_index_.contains(spell.id.value)) {
            LOG_WARN(magic, "Line {}: Duplicate spell ID {}", line_num, spell.id.value);
            ++errors;
            continue;
        }

        auto index = spells_.size();
        id_index_[spell.id.value] = index;
        name_index_[to_lower(spell.name)] = index;
        spells_.push_back(std::move(spell));
        ++loaded;
    }

    LOG_INFO(magic, "Loaded {} spells ({} errors)", loaded, errors);

    return result<size_t, std::string>::ok(loaded);
}

auto magic_registry::parse_spell_line(std::string_view line, int line_num)
    -> result<spell_template, std::string>
{
    // Expected format:
    // ID  Name  Type  ManaCost  CastTime  Range  Damage  IntScaling  MagReq  ...

    auto parts = split(line, '\t');
    if (parts.size() < 1) {
        parts = split(line, ' ');
    }

    if (parts.size() < 5) {
        return result<spell_template, std::string>::err(
            "Too few fields (need at least 5)"
        );
    }

    spell_template spell;

    spell.id = spell_id{static_cast<uint16_t>(parse_int(parts[0]))};
    if (!spell.id.is_valid()) {
        return result<spell_template, std::string>::err("Invalid spell ID");
    }

    spell.name = parts[1];
    if (spell.name.empty()) {
        return result<spell_template, std::string>::err("Empty spell name");
    }

    spell.type = static_cast<magic_type>(parse_int(parts[2]));
    spell.mana_cost = static_cast<int16_t>(parse_int(parts[3]));
    spell.cast_time_ms = static_cast<int16_t>(parse_int(parts[4]));

    // Optional fields
    if (parts.size() > 5) spell.range = static_cast<int16_t>(parse_int(parts[5]));
    if (parts.size() > 6) spell.base_damage = static_cast<int16_t>(parse_int(parts[6]));
    if (parts.size() > 7) spell.int_scaling = static_cast<int16_t>(parse_int(parts[7]));
    if (parts.size() > 8) spell.mag_level_req = static_cast<int16_t>(parse_int(parts[8]));
    if (parts.size() > 9) spell.int_req = static_cast<int16_t>(parse_int(parts[9]));
    if (parts.size() > 10) spell.area_radius = static_cast<int16_t>(parse_int(parts[10]));
    if (parts.size() > 11) spell.cooldown_ms = static_cast<int16_t>(parse_int(parts[11]));

    // Determine if offensive based on type
    switch (spell.type) {
        case magic_type::damage_spot:
        case magic_type::damage_area:
        case magic_type::poison:
        case magic_type::ice:
            spell.is_offensive = true;
            break;
        case magic_type::hp_up_spot:
        case magic_type::sp_up_spot:
        case magic_type::resurrection:
            spell.is_offensive = false;
            spell.can_hit_ally = true;
            spell.can_hit_enemy = false;
            break;
        default:
            break;
    }

    return result<spell_template, std::string>::ok(std::move(spell));
}

auto magic_registry::get(spell_id id) const -> const spell_template* {
    auto it = id_index_.find(id.value);
    if (it == id_index_.end()) {
        return nullptr;
    }
    return &spells_[it->second];
}

auto magic_registry::find_by_name(std::string_view name) const -> const spell_template* {
    auto it = name_index_.find(to_lower(std::string(name)));
    if (it == name_index_.end()) {
        return nullptr;
    }
    return &spells_[it->second];
}

auto magic_registry::by_type(magic_type type) const -> std::vector<const spell_template*> {
    std::vector<const spell_template*> result;
    for (const auto& spell : spells_) {
        if (spell.type == type) {
            result.push_back(&spell);
        }
    }
    return result;
}

auto magic_registry::offensive_spells() const -> std::vector<const spell_template*> {
    std::vector<const spell_template*> result;
    for (const auto& spell : spells_) {
        if (spell.is_offensive) {
            result.push_back(&spell);
        }
    }
    return result;
}

auto magic_registry::healing_spells() const -> std::vector<const spell_template*> {
    std::vector<const spell_template*> result;
    for (const auto& spell : spells_) {
        if (spell.type == magic_type::hp_up_spot) {
            result.push_back(&spell);
        }
    }
    return result;
}

auto magic_registry::buff_spells() const -> std::vector<const spell_template*> {
    std::vector<const spell_template*> result;
    for (const auto& spell : spells_) {
        if (spell.type == magic_type::berserk ||
            spell.type == magic_type::invisibility) {
            result.push_back(&spell);
        }
    }
    return result;
}

auto magic_registry::by_level(int magic_level) const -> std::vector<const spell_template*> {
    std::vector<const spell_template*> result;
    for (const auto& spell : spells_) {
        if (spell.mag_level_req <= magic_level) {
            result.push_back(&spell);
        }
    }
    return result;
}

auto magic_registry::count() const -> size_t {
    return spells_.size();
}

auto magic_registry::exists(spell_id id) const -> bool {
    return id_index_.contains(id.value);
}

auto magic_registry::all() const -> const std::vector<spell_template>& {
    return spells_;
}

}  // namespace hb
