#pragma once

// bt_loader.h
// Load behavior trees from YAML files

#include "npc/bt/bt_node.h"
#include "core/result.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace hb::npc::bt {

class bt_loader
{
public:
    auto load_from_file(const std::filesystem::path& path)
        -> result<std::string, std::string>;

    auto load_directory(const std::filesystem::path& dir)
        -> result<size_t, std::string>;

    [[nodiscard]] auto get_tree(std::string_view name) const -> const bt_node*;

    auto reload() -> result<size_t, std::string>;

    void clear();

private:
    std::unordered_map<std::string, std::unique_ptr<bt_node>> trees_;
    std::filesystem::path trees_dir_;
};

}  // namespace hb::npc::bt
