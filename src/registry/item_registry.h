#pragma once

// item_registry.h
// Read-only item template registry

#include "core/subsystem.h"
#include "core/result.h"
#include "registry/item_template.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>
#include <filesystem>

namespace hb {

// Item registry subsystem - loads and provides access to item templates
class item_registry : public subsystem {
public:
    item_registry();
    ~item_registry() override;

    // Subsystem interface
    [[nodiscard]] auto name() const -> std::string_view override { return "item_registry"; }
    void initialize() override;
    void shutdown() override;

    // Load item definitions from config file
    // Returns number of items loaded or error
    auto load_from_file(const std::filesystem::path& path) -> result<size_t, std::string>;

    // Get item by ID
    [[nodiscard]] auto get(item_id id) const -> const item_template*;

    // Get item by name (case-insensitive)
    [[nodiscard]] auto find_by_name(std::string_view name) const -> const item_template*;

    // Get all items of a specific type
    [[nodiscard]] auto by_type(item_type type) const -> std::vector<const item_template*>;

    // Get all items in a category
    [[nodiscard]] auto by_category(item_category category) const -> std::vector<const item_template*>;

    // Get all equippable items for a position
    [[nodiscard]] auto by_equip_pos(item_equip_pos pos) const -> std::vector<const item_template*>;

    // Get total count
    [[nodiscard]] auto count() const -> size_t;

    // Check if an item exists
    [[nodiscard]] auto exists(item_id id) const -> bool;

    // Iterate all items
    [[nodiscard]] auto all() const -> const std::vector<item_template>&;

private:
    // Parse a single item line from config (legacy CFG format)
    auto parse_item_line(std::string_view line, int line_num) -> result<item_template, std::string>;

    // Load items from YAML file
    auto load_from_yaml(const std::filesystem::path& path) -> result<size_t, std::string>;

    // Storage
    std::vector<item_template> items_;
    std::unordered_map<uint32_t, size_t> id_index_;     // id -> vector index
    std::unordered_map<std::string, size_t> name_index_; // lowercase name -> vector index
};

}  // namespace hb
