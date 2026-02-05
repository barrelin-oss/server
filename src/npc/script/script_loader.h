#pragma once

// script_loader.h
// YAML-based script loading with hot-reload

#include "npc/script/script_action.h"
#include "core/result.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace hb::npc::script {

class script_loader
{
public:
    auto load_from_file(const std::filesystem::path& path)
        -> result<npc_script, std::string>;

    auto load_directory(const std::filesystem::path& dir)
        -> result<size_t, std::string>;

    [[nodiscard]] auto get_script(std::string_view name) const -> const npc_script*;

    void clear();

    auto reload() -> result<size_t, std::string>;

private:
    std::unordered_map<std::string, npc_script> scripts_;
    std::filesystem::path scripts_dir_;
};

}  // namespace hb::npc::script
