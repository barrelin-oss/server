// quest_loader.h
// Loads the legacy Quest.cfg rows (converted to quests.yaml by tools/convert) into
// quest_templates. The data only uses two legacy types: 1 = hunt N of an NPC type,
// 7 = reach a place. The quest giver is the city hall officer of the quest's side
// (Kennedy for Aresden, William for Elvine): in the original only City Hall
// (iWho = 4) handed out quests, every other NPC returned "no quest".
#pragma once

#include "core/result.h"
#include "core/types.h"
#include "quest/quest.h"

#include <filesystem>
#include <functional>
#include <string_view>

namespace YAML
{
class Node;
}

namespace hb
{
class npc_registry;
}

namespace hb::quest
{

class quest_system;

// Resolves a legacy map name ("aresden", "huntzone2") to a map_id. Returns an
// invalid map_id (value 0) when the map is not loaded.
using map_resolver = std::function<map_id(std::string_view)>;

// Legacy quest_type values that appear in Quest.cfg
inline constexpr int legacy_quest_type_hunt = 1;
inline constexpr int legacy_quest_type_goplace = 7;

// Legacy reward_type sentinels (positive values are item ids; Gold is item 90)
inline constexpr int legacy_reward_exp = -1;
inline constexpr int legacy_reward_scaled_exp = -2;
inline constexpr int legacy_gold_item_id = 90;

// Converts one legacy row. Errors name the row and the field that failed.
[[nodiscard]] auto legacy_row_to_template(const YAML::Node& row,
                                          const npc_registry& npcs,
                                          const map_resolver& resolve_map) -> result<quest_template, std::string>;

// Loads every row of quests.yaml into the quest system. Rows that cannot be
// resolved (unknown NPC type, unknown map) are skipped with a warning; the
// returned count is the number registered.
auto load_legacy_quests(quest_system& quests,
                        const std::filesystem::path& yaml_path,
                        const npc_registry& npcs,
                        const map_resolver& resolve_map) -> result<size_t, std::string>;

} // namespace hb::quest
