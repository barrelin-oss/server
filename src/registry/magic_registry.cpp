// magic_registry.cpp
// Magic registry implementation - YAML parsing for spell configs

#include "registry/magic_registry.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <cctype>

namespace hb {

namespace {

auto to_lower(std::string str) -> std::string {
    std::transform(str.begin(), str.end(), str.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return str;
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
    LOG_INFO(magic, "Loading spells from: {}", path.string());

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        return result<size_t, std::string>::err(
            "Failed to parse magic YAML: " + std::string(e.what())
        );
    }

    auto spells_node = root["magic"];
    if (!spells_node || !spells_node.IsSequence()) {
        return result<size_t, std::string>::err("Missing or invalid 'magic' array in YAML");
    }

    size_t loaded = 0;
    size_t errors = 0;

    for (size_t i = 0; i < spells_node.size(); ++i) {
        const auto& node = spells_node[i];

        if (!node["id"] || !node["name"]) {
            LOG_WARN(magic, "Entry {}: missing id or name", i);
            ++errors;
            continue;
        }

        spell_template spell;

        spell.id = spell_id{static_cast<uint16_t>(node["id"].as<int>())};
        if (!spell.id.is_valid()) {
            LOG_WARN(magic, "Entry {}: invalid spell ID", i);
            ++errors;
            continue;
        }

        if (id_index_.contains(spell.id.value)) {
            LOG_WARN(magic, "Entry {}: duplicate spell ID {}", i, spell.id.value);
            ++errors;
            continue;
        }

        spell.name = node["name"].as<std::string>();
        spell.type = static_cast<magic_type>(node["type"].as<int>(1));
        spell.mana_cost = static_cast<int16_t>(node["mana_cost"].as<int>(0));
        spell.cast_time_ms = static_cast<int16_t>(node["delay"].as<int>(0));
        spell.int_req = static_cast<int16_t>(node["int_req"].as<int>(0));
        spell.range = static_cast<int16_t>(node["range1"].as<int>(0));

        if (node["duration"]) {
            spell.effect_duration = duration_ms(node["duration"].as<int>(0) * 1000);
        }

        // Parse effect dice for base_damage approximation (dice * (sides+1)/2 + bonus)
        if (node["effect1"]) {
            auto e = node["effect1"];
            int dice = e["dice"].as<int>(0);
            int sides = e["sides"].as<int>(0);
            int bonus = e["bonus"].as<int>(0);
            spell.base_damage = static_cast<int16_t>(dice * (sides + 1) / 2 + bonus);
        }

        // Determine offensive/targeting from type
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

        auto index = spells_.size();
        id_index_[spell.id.value] = index;
        name_index_[to_lower(spell.name)] = index;
        spells_.push_back(std::move(spell));
        ++loaded;
    }

    LOG_INFO(magic, "Loaded {} spells ({} errors)", loaded, errors);
    return result<size_t, std::string>::ok(loaded);
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
