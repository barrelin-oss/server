#pragma once

// dialog_config.h
// Dialog tree data structures for NPC conversations

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace hb::npc
{

// Action triggered by selecting a dialog option
enum class dialog_action : uint8_t
{
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

// Wire name of an action (docs/protocol/npc.md): the same string in player_interact_response
// and dialog_choice_response, so a client never sees the raw enum value.
[[nodiscard]] constexpr auto dialog_action_name(dialog_action action) -> std::string_view
{
    switch (action)
    {
    case dialog_action::goto_node:
        return "goto_node";
    case dialog_action::close:
        return "close";
    case dialog_action::open_shop:
        return "open_shop";
    case dialog_action::open_bank:
        return "open_bank";
    case dialog_action::open_quests:
        return "open_quests";
    case dialog_action::offer_citizenship:
        return "offer_citizenship";
    case dialog_action::select_crusade_job:
        return "select_crusade_job";
    case dialog_action::claim_rewards:
        return "claim_rewards";
    case dialog_action::open_manufacturing:
        return "open_manufacturing";
    case dialog_action::open_alchemy:
        return "open_alchemy";
    }
    return "close";
}

// A single selectable option in a dialog node
struct dialog_option
{
    std::string label;
    dialog_action action{dialog_action::goto_node};
    std::string next_node; // Only used when action == goto_node
};

// A dialog node - one screen of conversation
struct dialog_node
{
    std::string id;
    std::string text;
    std::vector<dialog_option> options;
};

// Complete dialog tree for an NPC
struct dialog_tree
{
    std::string npc_name;
    std::string greeting;
    std::string start_node{"start"};
    std::unordered_map<std::string, dialog_node> nodes;
};

} // namespace hb::npc
