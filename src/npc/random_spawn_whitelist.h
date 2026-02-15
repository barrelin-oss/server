#pragma once

// random_spawn_whitelist.h
// Whitelist of NPCs that can be randomly spawned
//
// Based on legacy CGame::MobGenerator() switch statement
// Only NPCs in this list can be selected by the random spawn system

#include "core/types.h"
#include "core/result.h"

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>
#include <filesystem>

namespace hb::npc
{

// Special attack type (matches legacy iKindSA values)
enum class special_attack_kind : uint8_t
{
    none = 0,
    melee = 1,  // Physical melee special
    ranged = 2, // Ranged attack
    poison = 3, // Poison effect
    stun = 4,   // Stunning attack
    magic = 5,  // Magic-based special
    area = 6,   // Area of effect
    buff = 7,   // Self-buff ability
    summon = 8  // Summons minions
};

// Entry for a randomly spawnable NPC
struct random_spawn_entry
{
    std::string name;           // NPC name (key for lookup)
    npc_id template_id{0};      // Template ID (0 = not spawnable)
    int special_attack_prob{0}; // Probability of special attack (0-100)
    special_attack_kind special_attack{special_attack_kind::none};
    bool enabled{true}; // Can be disabled without removal
};

// Whitelist of NPCs that can be randomly spawned
class random_spawn_whitelist
{
public:
    random_spawn_whitelist() = default;

    // Load whitelist from YAML file
    auto load_from_yaml(const std::filesystem::path& path) -> result<size_t, std::string>;

    // Parse whitelist from YAML node (called by spawn_rule_engine)
    auto load_from_node(const void* yaml_node) -> result<size_t, std::string>;

    // Check if an NPC is allowed to be randomly spawned
    [[nodiscard]] auto is_allowed(std::string_view name) const -> bool;
    [[nodiscard]] auto is_allowed(npc_id id) const -> bool;

    // Get entry by name (returns nullopt if not in whitelist)
    [[nodiscard]] auto get(std::string_view name) const -> std::optional<random_spawn_entry>;

    // Get entry by template ID (returns nullopt if not in whitelist)
    [[nodiscard]] auto get_by_id(npc_id id) const -> std::optional<random_spawn_entry>;

    // Get all entries
    [[nodiscard]] auto all() const -> const std::vector<random_spawn_entry>& { return entries_; }

    // Get count of spawnable entries (excluding disabled and id=0)
    [[nodiscard]] auto spawnable_count() const -> size_t;

    // Clear all entries
    void clear();

private:
    std::vector<random_spawn_entry> entries_;
    std::unordered_map<std::string, size_t> name_index_;
    std::unordered_map<uint16_t, size_t> id_index_;
};

} // namespace hb::npc
