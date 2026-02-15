#pragma once

// bt_leaves.h
// Leaf nodes (conditions and actions) for behavior trees

#include "npc/bt/bt_node.h"
#include "core/types.h"

#include <string>

namespace hb::npc::bt
{

// --- Condition leaves ---

struct has_target : bt_node
{
    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "has_target"; }
};

struct hp_below : bt_node
{
    float threshold{0.3f}; // 0.0 - 1.0

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "hp_below"; }
};

struct target_in_range : bt_node
{
    int range{1};

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "target_in_range"; }
};

struct is_near_spawn : bt_node
{
    int range{3};

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "is_near_spawn"; }
};

struct random_chance : bt_node
{
    int percent{50}; // 0-100

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "random_chance"; }
};

// --- Action leaves ---

struct find_target : bt_node
{
    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "find_target"; }
};

struct chase_target : bt_node
{
    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "chase_target"; }
};

struct attack_target : bt_node
{
    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "attack_target"; }
};

struct flee_from_target : bt_node
{
    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "flee_from_target"; }
};

struct wander : bt_node
{
    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "wander"; }
};

struct return_home : bt_node
{
    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "return_home"; }
};

struct call_help : bt_node
{
    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "call_help"; }
};

struct run_script : bt_node
{
    std::string script_name;

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "run_script"; }
};

struct idle : bt_node
{
    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "idle"; }
};

struct heal_self : bt_node
{
    int amount{0};
    float percent{0.0f};

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "heal_self"; }
};

} // namespace hb::npc::bt
