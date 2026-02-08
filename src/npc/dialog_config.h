#pragma once

// dialog_config.h
// Dialog tree data structures for NPC conversations

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace hb::npc {

// Action triggered by selecting a dialog option
enum class dialog_action : uint8_t {
    goto_node = 0,
    close = 1,
    open_shop = 2,
    open_bank = 3,
    open_quests = 4,
    offer_citizenship = 5,
    select_crusade_job = 6,
    claim_rewards = 7,
    open_manufacturing = 8,
    open_alchemy = 9,
};

// A single selectable option in a dialog node
struct dialog_option {
    std::string label;
    dialog_action action{dialog_action::goto_node};
    std::string next_node;  // Only used when action == goto_node
};

// A dialog node - one screen of conversation
struct dialog_node {
    std::string id;
    std::string text;
    std::vector<dialog_option> options;
};

// Complete dialog tree for an NPC
struct dialog_tree {
    std::string npc_name;
    std::string greeting;
    std::string start_node{"start"};
    std::unordered_map<std::string, dialog_node> nodes;
};

}  // namespace hb::npc
