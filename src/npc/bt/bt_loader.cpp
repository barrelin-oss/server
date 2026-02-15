// bt_loader.cpp
// Load behavior trees from YAML files

#include "npc/bt/bt_loader.h"
#include "npc/bt/bt_leaves.h"
#include "npc/bt/bt_context.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>

namespace hb::npc::bt
{

namespace
{

auto parse_node(const YAML::Node& yaml) -> std::unique_ptr<bt_node>;

auto parse_children(const YAML::Node& yaml) -> std::vector<std::unique_ptr<bt_node>>
{
    std::vector<std::unique_ptr<bt_node>> children;
    if (!yaml["children"] || !yaml["children"].IsSequence())
        return children;

    for (const auto& child_yaml : yaml["children"])
    {
        auto child = parse_node(child_yaml);
        if (child)
        {
            children.push_back(std::move(child));
        }
    }
    return children;
}

auto parse_single_child(const YAML::Node& yaml) -> std::unique_ptr<bt_node>
{
    if (yaml["child"])
    {
        return parse_node(yaml["child"]);
    }
    // Also check children[0] as fallback
    if (yaml["children"] && yaml["children"].IsSequence() && yaml["children"].size() > 0)
    {
        return parse_node(yaml["children"][0]);
    }
    return nullptr;
}

auto parse_node(const YAML::Node& yaml) -> std::unique_ptr<bt_node>
{
    if (!yaml["type"])
        return nullptr;

    std::string type = yaml["type"].as<std::string>();

    // Composite nodes
    if (type == "sequence")
    {
        auto node = std::make_unique<sequence_node>();
        node->children = parse_children(yaml);
        return node;
    }
    if (type == "selector")
    {
        auto node = std::make_unique<selector_node>();
        node->children = parse_children(yaml);
        return node;
    }
    if (type == "parallel")
    {
        auto node = std::make_unique<parallel_node>();
        node->children = parse_children(yaml);
        node->success_threshold = yaml["success_threshold"].as<int>(1);
        return node;
    }

    // Decorator nodes
    if (type == "inverter")
    {
        auto node = std::make_unique<inverter_node>();
        node->child = parse_single_child(yaml);
        return node;
    }
    if (type == "repeat")
    {
        auto node = std::make_unique<repeat_node>();
        node->child = parse_single_child(yaml);
        node->count = yaml["count"].as<int>(1);
        return node;
    }
    if (type == "cooldown")
    {
        auto node = std::make_unique<cooldown_node>();
        node->child = parse_single_child(yaml);
        node->cooldown_ms = yaml["cooldown_ms"].as<int32_t>(1000);
        return node;
    }
    if (type == "always_succeed")
    {
        auto node = std::make_unique<always_succeed_node>();
        node->child = parse_single_child(yaml);
        return node;
    }

    // Condition leaves
    if (type == "has_target")
    {
        return std::make_unique<has_target>();
    }
    if (type == "hp_below")
    {
        auto node = std::make_unique<hp_below>();
        node->threshold = yaml["threshold"].as<float>(0.3f);
        return node;
    }
    if (type == "target_in_range")
    {
        auto node = std::make_unique<target_in_range>();
        node->range = yaml["range"].as<int>(1);
        return node;
    }
    if (type == "is_near_spawn")
    {
        auto node = std::make_unique<is_near_spawn>();
        node->range = yaml["range"].as<int>(3);
        return node;
    }
    if (type == "random_chance")
    {
        auto node = std::make_unique<random_chance>();
        node->percent = yaml["percent"].as<int>(50);
        return node;
    }

    // Action leaves
    if (type == "find_target")
    {
        return std::make_unique<find_target>();
    }
    if (type == "chase_target")
    {
        return std::make_unique<chase_target>();
    }
    if (type == "attack_target")
    {
        return std::make_unique<attack_target>();
    }
    if (type == "flee_from_target")
    {
        return std::make_unique<flee_from_target>();
    }
    if (type == "wander")
    {
        return std::make_unique<wander>();
    }
    if (type == "return_home")
    {
        return std::make_unique<return_home>();
    }
    if (type == "call_help")
    {
        return std::make_unique<call_help>();
    }
    if (type == "run_script")
    {
        auto node = std::make_unique<run_script>();
        node->script_name = yaml["script"].as<std::string>("");
        return node;
    }
    if (type == "idle")
    {
        return std::make_unique<idle>();
    }
    if (type == "heal_self")
    {
        auto node = std::make_unique<heal_self>();
        node->amount = yaml["amount"].as<int>(0);
        node->percent = yaml["percent"].as<float>(0.0f);
        return node;
    }

    LOG_WARN(general, "Unknown behavior tree node type: {}", type);
    return nullptr;
}

} // anonymous namespace

auto bt_loader::load_from_file(const std::filesystem::path& path) -> result<std::string, std::string>
{
    try
    {
        YAML::Node config = YAML::LoadFile(path.string());

        std::string name = config["name"].as<std::string>(path.stem().string());

        if (!config["root"])
        {
            return result<std::string, std::string>::err("Missing 'root' node in behavior tree: " + path.string());
        }

        auto root = parse_node(config["root"]);
        if (!root)
        {
            return result<std::string, std::string>::err("Failed to parse root node in: " + path.string());
        }

        trees_[name] = std::move(root);
        return result<std::string, std::string>::ok(name);
    }
    catch (const YAML::Exception& e)
    {
        return result<std::string, std::string>::err(std::string("YAML error loading BT: ") + e.what());
    }
    catch (const std::exception& e)
    {
        return result<std::string, std::string>::err(std::string("Error loading BT: ") + e.what());
    }
}

auto bt_loader::load_directory(const std::filesystem::path& dir) -> result<size_t, std::string>
{
    trees_dir_ = dir;

    if (!std::filesystem::exists(dir))
    {
        return result<size_t, std::string>::ok(size_t{0});
    }

    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file())
            continue;

        auto ext = entry.path().extension().string();
        if (ext != ".yaml" && ext != ".yml")
            continue;

        auto res = load_from_file(entry.path());
        if (res.is_ok())
        {
            ++count;
        }
        else
        {
            LOG_WARN(general, "Failed to load behavior tree {}: {}", entry.path().string(), res.error());
        }
    }

    LOG_INFO(general, "Loaded {} behavior trees from {}", count, dir.string());
    return result<size_t, std::string>::ok(count);
}

auto bt_loader::get_tree(std::string_view name) const -> const bt_node*
{
    auto it = trees_.find(std::string(name));
    return it != trees_.end() ? it->second.get() : nullptr;
}

auto bt_loader::reload() -> result<size_t, std::string>
{
    if (trees_dir_.empty())
    {
        return result<size_t, std::string>::err("No behavior tree directory set");
    }

    trees_.clear();
    return load_directory(trees_dir_);
}

void bt_loader::clear()
{
    trees_.clear();
}

} // namespace hb::npc::bt
