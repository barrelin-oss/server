#pragma once

// magic_registry.h
// Read-only spell/magic template registry

#include "core/subsystem.h"
#include "core/result.h"
#include "registry/spell_template.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <filesystem>

namespace hb {

// Magic registry subsystem
class magic_registry : public subsystem {
public:
    magic_registry();
    ~magic_registry() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "magic_registry"; }
    void initialize() override;
    void shutdown() override;

    // Load spell definitions from config file
    auto load_from_file(const std::filesystem::path& path) -> result<size_t, std::string>;

    // Get spell by ID
    [[nodiscard]] auto get(spell_id id) const -> const spell_template*;

    // Get spell by name (case-insensitive)
    [[nodiscard]] auto find_by_name(std::string_view name) const -> const spell_template*;

    // Get all spells of a specific type
    [[nodiscard]] auto by_type(magic_type type) const -> std::vector<const spell_template*>;

    // Get all offensive spells
    [[nodiscard]] auto offensive_spells() const -> std::vector<const spell_template*>;

    // Get all healing spells
    [[nodiscard]] auto healing_spells() const -> std::vector<const spell_template*>;

    // Get all buff spells
    [[nodiscard]] auto buff_spells() const -> std::vector<const spell_template*>;

    // Get spells available at a given magic level
    [[nodiscard]] auto by_level(int magic_level) const -> std::vector<const spell_template*>;

    // Get total count
    [[nodiscard]] auto count() const -> size_t;

    // Check if a spell exists
    [[nodiscard]] auto exists(spell_id id) const -> bool;

    // Iterate all spells
    [[nodiscard]] auto all() const -> const std::vector<spell_template>&;

private:
    std::vector<spell_template> spells_;
    std::unordered_map<uint16_t, size_t> id_index_;
    std::unordered_map<std::string, size_t> name_index_;
};

}  // namespace hb
