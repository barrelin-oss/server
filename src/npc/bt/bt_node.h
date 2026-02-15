#pragma once

// bt_node.h
// Behavior tree node types: composites and decorators

#include <memory>
#include <string_view>
#include <vector>

namespace hb::npc
{
struct npc;
}

namespace hb::npc::bt
{

// Forward declaration
struct bt_context;

enum class node_status : uint8_t
{
    success,
    failure,
    running,
};

// Base node
struct bt_node
{
    virtual ~bt_node() = default;
    virtual auto tick(npc& npc_ref, bt_context& ctx) -> node_status = 0;
    virtual auto type_name() const -> std::string_view = 0;
};

// --- Composite nodes ---

// Run children in order, fail on first failure
struct sequence_node : bt_node
{
    std::vector<std::unique_ptr<bt_node>> children;

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override
    {
        for (auto& child : children)
        {
            auto status = child->tick(npc_ref, ctx);
            if (status != node_status::success)
            {
                return status;
            }
        }
        return node_status::success;
    }

    auto type_name() const -> std::string_view override { return "sequence"; }
};

// Run children in order, succeed on first success
struct selector_node : bt_node
{
    std::vector<std::unique_ptr<bt_node>> children;

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override
    {
        for (auto& child : children)
        {
            auto status = child->tick(npc_ref, ctx);
            if (status != node_status::failure)
            {
                return status;
            }
        }
        return node_status::failure;
    }

    auto type_name() const -> std::string_view override { return "selector"; }
};

// Run all children, succeed if threshold met
struct parallel_node : bt_node
{
    std::vector<std::unique_ptr<bt_node>> children;
    int success_threshold{1};

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override
    {
        int success_count = 0;
        int failure_count = 0;
        bool has_running = false;

        for (auto& child : children)
        {
            auto status = child->tick(npc_ref, ctx);
            if (status == node_status::success)
                ++success_count;
            else if (status == node_status::failure)
                ++failure_count;
            else
                has_running = true;
        }

        if (success_count >= success_threshold)
            return node_status::success;
        if (failure_count > static_cast<int>(children.size()) - success_threshold)
            return node_status::failure;
        if (has_running)
            return node_status::running;
        return node_status::failure;
    }

    auto type_name() const -> std::string_view override { return "parallel"; }
};

// --- Decorator nodes ---

// Invert child result
struct inverter_node : bt_node
{
    std::unique_ptr<bt_node> child;

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override
    {
        if (!child)
            return node_status::failure;
        auto status = child->tick(npc_ref, ctx);
        if (status == node_status::success)
            return node_status::failure;
        if (status == node_status::failure)
            return node_status::success;
        return node_status::running;
    }

    auto type_name() const -> std::string_view override { return "inverter"; }
};

// Repeat child N times
struct repeat_node : bt_node
{
    std::unique_ptr<bt_node> child;
    int count{1};

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "repeat"; }
};

// Only run child if cooldown elapsed
struct cooldown_node : bt_node
{
    std::unique_ptr<bt_node> child;
    int32_t cooldown_ms{1000};

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override;
    auto type_name() const -> std::string_view override { return "cooldown"; }
};

// Always succeed regardless of child result
struct always_succeed_node : bt_node
{
    std::unique_ptr<bt_node> child;

    auto tick(npc& npc_ref, bt_context& ctx) -> node_status override
    {
        if (child)
            child->tick(npc_ref, ctx);
        return node_status::success;
    }

    auto type_name() const -> std::string_view override { return "always_succeed"; }
};

} // namespace hb::npc::bt
