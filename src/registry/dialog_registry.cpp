// dialog_registry.cpp
// Dialog registry implementation - YAML parsing for dialog trees

#include "registry/dialog_registry.h"
#include "core/logger.h"

#include <yaml-cpp/yaml.h>

namespace hb {

namespace {

auto parse_dialog_action(const std::string& action_str) -> npc::dialog_action
{
    if (action_str == "close") return npc::dialog_action::close;
    if (action_str == "open_shop") return npc::dialog_action::open_shop;
    if (action_str == "open_bank") return npc::dialog_action::open_bank;
    if (action_str == "open_quests") return npc::dialog_action::open_quests;
    if (action_str == "offer_citizenship") return npc::dialog_action::offer_citizenship;
    if (action_str == "select_crusade_job") return npc::dialog_action::select_crusade_job;
    if (action_str == "claim_rewards") return npc::dialog_action::claim_rewards;
    return npc::dialog_action::goto_node;
}

}  // namespace

dialog_registry::dialog_registry() = default;
dialog_registry::~dialog_registry() = default;

void dialog_registry::initialize()
{
    LOG_INFO(general, "Dialog registry initialized");
    set_initialized(true);
}

void dialog_registry::shutdown()
{
    LOG_INFO(general, "Dialog registry shut down ({} dialogs)", dialogs_.size());
    dialogs_.clear();
    set_initialized(false);
}

auto dialog_registry::load_from_file(const std::filesystem::path& path)
    -> result<size_t, std::string>
{
    LOG_INFO(general, "Loading dialogs from: {}", path.string());

    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        return result<size_t, std::string>::err(
            "Failed to parse dialogs YAML: " + std::string(e.what())
        );
    }

    if (!root["dialogs"] || !root["dialogs"].IsMap())
    {
        return result<size_t, std::string>::err("Missing or invalid 'dialogs' section");
    }

    size_t count = 0;
    for (const auto& pair : root["dialogs"])
    {
        auto npc_name = pair.first.as<std::string>();
        npc::dialog_tree tree;
        tree.npc_name = npc_name;

        auto& node = pair.second;

        if (node["greeting"])
        {
            tree.greeting = node["greeting"].as<std::string>();
        }
        if (node["start_node"])
        {
            tree.start_node = node["start_node"].as<std::string>();
        }

        // Parse nodes
        if (node["nodes"] && node["nodes"].IsMap())
        {
            for (const auto& node_pair : node["nodes"])
            {
                auto node_id = node_pair.first.as<std::string>();
                npc::dialog_node dialog_node;
                dialog_node.id = node_id;

                auto& n = node_pair.second;

                if (n["text"])
                {
                    dialog_node.text = n["text"].as<std::string>();
                }

                if (n["options"] && n["options"].IsSequence())
                {
                    for (const auto& opt : n["options"])
                    {
                        npc::dialog_option option;
                        if (opt["label"])
                        {
                            option.label = opt["label"].as<std::string>();
                        }
                        if (opt["action"])
                        {
                            option.action = parse_dialog_action(opt["action"].as<std::string>());
                        }
                        if (opt["next"])
                        {
                            option.next_node = opt["next"].as<std::string>();
                            // If 'next' is set but no explicit action, it's goto_node
                            if (!opt["action"])
                            {
                                option.action = npc::dialog_action::goto_node;
                            }
                        }
                        dialog_node.options.push_back(std::move(option));
                    }
                }

                tree.nodes[node_id] = std::move(dialog_node);
            }
        }

        LOG_DEBUG(general, "  Dialog '{}': {} nodes", npc_name, tree.nodes.size());
        dialogs_[npc_name] = std::move(tree);
        ++count;
    }

    LOG_INFO(general, "Loaded {} dialog trees", count);
    return result<size_t, std::string>::ok(count);
}

auto dialog_registry::get_dialog(std::string_view npc_name) const -> const npc::dialog_tree*
{
    auto it = dialogs_.find(std::string(npc_name));
    return it != dialogs_.end() ? &it->second : nullptr;
}

auto dialog_registry::get_node(std::string_view npc_name, std::string_view node_id) const
    -> const npc::dialog_node*
{
    auto* tree = get_dialog(npc_name);
    if (!tree) return nullptr;

    auto it = tree->nodes.find(std::string(node_id));
    return it != tree->nodes.end() ? &it->second : nullptr;
}

}  // namespace hb
