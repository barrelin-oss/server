#pragma once

// boss_loader.h
// Load boss configurations from YAML files

#include "npc/boss/boss_phase.h"
#include "core/result.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace hb::npc::boss
{

class boss_loader
{
public:
    auto load_from_file(const std::filesystem::path& path) -> result<boss_config, std::string>;

    auto load_directory(const std::filesystem::path& dir) -> result<size_t, std::string>;

    [[nodiscard]] auto get_config(std::string_view name) const -> const boss_config*;

    auto reload() -> result<size_t, std::string>;

    void clear();

private:
    std::unordered_map<std::string, boss_config> configs_;
    std::filesystem::path configs_dir_;
};

} // namespace hb::npc::boss
