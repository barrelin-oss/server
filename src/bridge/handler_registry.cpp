// handler_registry.cpp
// Implementation of handler registration helpers

#include "bridge/handler_registry.h"
#include "bridge/message_router.h"
#include "network/network_subsystem.h"
#include "core/subsystem.h"
#include "core/logger.h"

#include <unordered_set>
#include <mutex>

namespace hb::bridge {

// Track fully migrated message types
namespace {
    std::unordered_set<protocol::message_id> g_fully_migrated;
    std::mutex g_migrated_mutex;

    // Estimated total message types (for progress calculation)
    // Based on NetMessages.h analysis
    constexpr size_t estimated_total_messages = 200;
}

// ========== Response Helpers ==========

void send_response(const handler_context& ctx, const protocol::message_writer& response) {
    auto* network = subsystems().get<network::network_subsystem>();
    if (network) {
        network->send(ctx.connection, response.data());
    }
}

void send_response(const handler_context& ctx,
                   protocol::message_id response_id,
                   std::function<void(protocol::message_writer&)> write_data) {
    auto writer = build_response(response_id, std::move(write_data));
    send_response(ctx, writer);
}

auto build_response(protocol::message_id msg_id,
                    std::function<void(protocol::message_writer&)> write_data)
    -> protocol::message_writer {
    protocol::message_writer writer;
    writer.write_u32(static_cast<uint32_t>(msg_id));
    if (write_data) {
        write_data(writer);
    }
    return writer;
}

// ========== Migration Helpers ==========

auto should_use_modern(protocol::message_id msg_id) -> bool {
    return router().is_handler_enabled(msg_id);
}

void mark_fully_migrated(protocol::message_id msg_id) {
    std::lock_guard lock(g_migrated_mutex);
    g_fully_migrated.insert(msg_id);
    LOG_INFO(proto_bridge, "Message {} marked as fully migrated",
             protocol::message_id_string(msg_id));
}

auto is_fully_migrated(protocol::message_id msg_id) -> bool {
    std::lock_guard lock(g_migrated_mutex);
    return g_fully_migrated.contains(msg_id);
}

auto get_migrated_messages() -> std::vector<protocol::message_id> {
    std::lock_guard lock(g_migrated_mutex);
    return std::vector<protocol::message_id>(
        g_fully_migrated.begin(),
        g_fully_migrated.end()
    );
}

auto migration_progress() -> float {
    auto modernized = router().modernized_count();
    if (estimated_total_messages == 0) return 0.0f;
    return static_cast<float>(modernized) / static_cast<float>(estimated_total_messages) * 100.0f;
}

// ========== Debug Utilities ==========

void log_registered_handlers() {
    LOG_INFO(proto_bridge, "=== Registered Message Handlers ===");

    auto handlers = router().get_all_handlers();
    if (handlers.empty()) {
        LOG_INFO(proto_bridge, "  (no handlers registered)");
        return;
    }

    for (const auto* info : handlers) {
        LOG_INFO(proto_bridge, "  {} -> {} [{}] {}",
                 protocol::message_id_string(info->msg_id),
                 info->subsystem,
                 info->enabled ? "enabled" : "disabled",
                 is_fully_migrated(info->msg_id) ? "(migrated)" : "");
    }

    LOG_INFO(proto_bridge, "=== {} handlers registered, {:.1f}% migration progress ===",
             handlers.size(), migration_progress());
}

void log_handler_stats() {
    LOG_INFO(proto_bridge, "=== Message Handler Statistics ===");

    auto global = router().stats();
    LOG_INFO(proto_bridge, "  Total messages:    {}", global.total_messages);
    LOG_INFO(proto_bridge, "  Modern handled:    {} ({:.1f}%)",
             global.modern_handled,
             global.total_messages > 0
                ? (100.0 * global.modern_handled / global.total_messages) : 0.0);
    LOG_INFO(proto_bridge, "  Legacy fallback:   {} ({:.1f}%)",
             global.legacy_fallback,
             global.total_messages > 0
                ? (100.0 * global.legacy_fallback / global.total_messages) : 0.0);
    LOG_INFO(proto_bridge, "  Errors:            {}", global.errors);
    LOG_INFO(proto_bridge, "  Disconnects:       {}", global.disconnects);
    LOG_INFO(proto_bridge, "  Total proc time:   {}ms",
             std::chrono::duration_cast<std::chrono::milliseconds>(
                 global.total_processing_time).count());

    auto msg_stats = router().all_message_stats();
    if (!msg_stats.empty()) {
        LOG_INFO(proto_bridge, "--- Per-Message Stats (top 10 by count) ---");

        // Sort by total count
        std::sort(msg_stats.begin(), msg_stats.end(),
                  [](const auto& a, const auto& b) {
                      return a.total_count > b.total_count;
                  });

        size_t count = 0;
        for (const auto& stats : msg_stats) {
            if (count++ >= 10) break;
            LOG_INFO(proto_bridge, "  {}: {} total, {} success, {} fallback, {} errors, max {}us",
                     protocol::message_id_string(stats.msg_id),
                     stats.total_count,
                     stats.success_count,
                     stats.fallback_count,
                     stats.error_count,
                     std::chrono::duration_cast<std::chrono::microseconds>(
                         stats.max_time).count());
        }
    }

    LOG_INFO(proto_bridge, "==========================================");
}

}  // namespace hb::bridge
