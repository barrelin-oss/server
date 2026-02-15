// bt_leaves.cpp
// Leaf node implementations for behavior trees

#include "npc/bt/bt_leaves.h"
#include "npc/bt/bt_context.h"
#include "npc/npc.h"
#include "npc/npc_system.h"
#include "world/position.h"

#include <random>
#include <chrono>

namespace hb::npc::bt
{

namespace
{
thread_local std::mt19937 rng{std::random_device{}()};

auto random_int(int min, int max) -> int
{
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

auto random_direction() -> hb::world::direction
{
    return static_cast<hb::world::direction>(random_int(0, 7));
}
} // namespace

// --- Condition leaves ---

auto has_target::tick(npc& npc_ref, bt_context& /*ctx*/) -> node_status
{
    return npc_ref.ai_state.target.is_valid() ? node_status::success : node_status::failure;
}

auto hp_below::tick(npc& npc_ref, bt_context& /*ctx*/) -> node_status
{
    float hp_pct = npc_ref.max_hp > 0 ? static_cast<float>(npc_ref.hp) / static_cast<float>(npc_ref.max_hp) : 0.0f;
    return hp_pct < threshold ? node_status::success : node_status::failure;
}

auto target_in_range::tick(npc& npc_ref, bt_context& ctx) -> node_status
{
    if (!npc_ref.ai_state.target.is_valid())
        return node_status::failure;
    if (!ctx.npc_sys)
        return node_status::failure;

    auto* target_npc = ctx.npc_sys->get_npc(npc_ref.ai_state.target);
    if (target_npc)
    {
        int dist = npc_ref.pos.chebyshev_distance(target_npc->pos);
        return dist <= range ? node_status::success : node_status::failure;
    }

    // Try player position via npc_system helper
    // Use the stored target_position if available
    if (npc_ref.ai_state.target_position.x != 0 || npc_ref.ai_state.target_position.y != 0)
    {
        int dist = npc_ref.pos.chebyshev_distance(npc_ref.ai_state.target_position);
        return dist <= range ? node_status::success : node_status::failure;
    }

    return node_status::failure;
}

auto is_near_spawn::tick(npc& npc_ref, bt_context& /*ctx*/) -> node_status
{
    int dist = npc_ref.pos.chebyshev_distance(npc_ref.ai_state.spawn_point);
    return dist <= range ? node_status::success : node_status::failure;
}

auto random_chance::tick(npc& /*npc_ref*/, bt_context& /*ctx*/) -> node_status
{
    return random_int(1, 100) <= percent ? node_status::success : node_status::failure;
}

// --- Action leaves ---

auto find_target::tick(npc& npc_ref, bt_context& ctx) -> node_status
{
    if (!ctx.npc_sys)
        return node_status::failure;

    auto target = ctx.npc_sys->find_aggro_target_for(npc_ref);
    if (target.is_valid())
    {
        npc_ref.ai_state.target = target;
        return node_status::success;
    }
    return node_status::failure;
}

auto chase_target::tick(npc& npc_ref, bt_context& ctx) -> node_status
{
    if (!npc_ref.ai_state.target.is_valid())
        return node_status::failure;
    if (!ctx.npc_sys)
        return node_status::failure;

    // Check spawn distance limit
    int spawn_dist = npc_ref.pos.chebyshev_distance(npc_ref.ai_state.spawn_point);
    if (spawn_dist > npc_ref.ai.chase_range)
    {
        npc_ref.ai_state.clear_target();
        return node_status::failure;
    }

    // Move towards target
    ctx.npc_sys->move_npc_towards_target(npc_ref);
    return node_status::running;
}

auto attack_target::tick(npc& npc_ref, bt_context& ctx) -> node_status
{
    if (!npc_ref.ai_state.target.is_valid())
        return node_status::failure;
    if (!ctx.npc_sys)
        return node_status::failure;

    // Check attack cooldown
    auto now = std::chrono::steady_clock::now();
    auto time_since_attack =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - npc_ref.ai_state.last_attack_time).count();

    int attack_cooldown = npc_ref.attack_speed > 0 ? 1000 * 100 / npc_ref.attack_speed : 1000;

    if (time_since_attack < attack_cooldown)
    {
        return node_status::running;
    }

    ctx.npc_sys->perform_attack(npc_ref, npc_ref.ai_state.target);
    npc_ref.ai_state.last_attack_time = now;
    return node_status::success;
}

auto flee_from_target::tick(npc& npc_ref, bt_context& ctx) -> node_status
{
    if (!npc_ref.ai_state.target.is_valid())
        return node_status::failure;
    if (!ctx.npc_sys)
        return node_status::failure;

    ctx.npc_sys->move_npc_away_from_target(npc_ref);
    return node_status::running;
}

auto wander::tick(npc& npc_ref, bt_context& ctx) -> node_status
{
    if (!ctx.npc_sys)
        return node_status::failure;

    auto dir = random_direction();
    auto new_pos = hb::world::move_in_direction(npc_ref.pos, dir);

    int spawn_dist = new_pos.chebyshev_distance(npc_ref.ai_state.spawn_point);
    if (spawn_dist <= npc_ref.ai.wander_range)
    {
        ctx.npc_sys->try_move(npc_ref, new_pos);
    }
    return node_status::success;
}

auto return_home::tick(npc& npc_ref, bt_context& ctx) -> node_status
{
    if (!ctx.npc_sys)
        return node_status::failure;

    int spawn_dist = npc_ref.pos.chebyshev_distance(npc_ref.ai_state.spawn_point);
    if (spawn_dist <= 1)
    {
        npc_ref.heal(npc_ref.max_hp / 10);
        return node_status::success;
    }

    ctx.npc_sys->move_npc_towards_position(npc_ref, npc_ref.ai_state.spawn_point);
    return node_status::running;
}

auto call_help::tick(npc& npc_ref, bt_context& ctx) -> node_status
{
    if (!ctx.npc_sys)
        return node_status::failure;
    if (!npc_ref.ai_state.target.is_valid())
        return node_status::failure;

    ctx.npc_sys->call_for_help_from(npc_ref, npc_ref.ai_state.target);
    return node_status::success;
}

auto run_script::tick(npc& npc_ref, bt_context& ctx) -> node_status
{
    if (!ctx.npc_sys)
        return node_status::failure;
    if (script_name.empty())
        return node_status::failure;

    // Check if script is already running
    if (ctx.npc_sys->get_script_executor().is_running(npc_ref.entity_id))
    {
        return node_status::running;
    }

    ctx.npc_sys->start_script(npc_ref.entity_id, script_name);
    return node_status::success;
}

auto idle::tick(npc& /*npc_ref*/, bt_context& /*ctx*/) -> node_status
{
    return node_status::success;
}

auto heal_self::tick(npc& npc_ref, bt_context& /*ctx*/) -> node_status
{
    if (amount > 0)
    {
        npc_ref.heal(amount);
    }
    else if (percent > 0.0f)
    {
        int heal_amount = static_cast<int>(static_cast<float>(npc_ref.max_hp) * percent);
        npc_ref.heal(heal_amount);
    }
    return node_status::success;
}

// --- Decorator implementations ---

auto repeat_node::tick(npc& npc_ref, bt_context& ctx) -> node_status
{
    if (!child)
        return node_status::failure;

    auto& run_count = ctx.run_counts[this];

    for (int i = run_count; i < count; ++i)
    {
        auto status = child->tick(npc_ref, ctx);
        if (status == node_status::running)
        {
            run_count = i;
            return node_status::running;
        }
        if (status == node_status::failure)
        {
            run_count = 0;
            return node_status::failure;
        }
    }

    run_count = 0;
    return node_status::success;
}

auto cooldown_node::tick(npc& npc_ref, bt_context& ctx) -> node_status
{
    if (!child)
        return node_status::failure;

    auto now = std::chrono::steady_clock::now();
    auto& last_run = ctx.cooldowns[this];

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_run).count();
    if (last_run != std::chrono::steady_clock::time_point{} && elapsed < cooldown_ms)
    {
        return node_status::failure;
    }

    auto status = child->tick(npc_ref, ctx);
    if (status != node_status::failure)
    {
        last_run = now;
    }
    return status;
}

} // namespace hb::npc::bt
