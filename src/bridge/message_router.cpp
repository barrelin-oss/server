// message_router.cpp
// Implementation of message routing between modern subsystems and legacy code

#include "bridge/message_router.h"
#include "core/logger.h"
#include "protocol/protocol.h"

#include <unordered_map>
#include <mutex>
#include <shared_mutex>

namespace hb::bridge {

// Implementation details
struct message_router::impl {
    // Handler storage (msg_id -> handler_info)
    std::unordered_map<protocol::message_id, handler_info> handlers;

    // Connection to session/player mappings
    std::unordered_map<connection_id, session_id> connection_sessions;
    std::unordered_map<connection_id, player_id> connection_players;

    // Legacy fallback handler
    legacy_fallback fallback;

    // Statistics
    router_stats global_stats;
    std::unordered_map<protocol::message_id, message_stats> msg_stats;

    // Thread safety
    mutable std::shared_mutex handler_mutex;
    mutable std::shared_mutex mapping_mutex;
    mutable std::mutex stats_mutex;

    // Configuration
    bool verbose_logging{false};
};

message_router::message_router()
    : impl_(std::make_unique<impl>())
{
}

message_router::~message_router() = default;

message_router::message_router(message_router&&) noexcept = default;
auto message_router::operator=(message_router&&) noexcept -> message_router& = default;

// ========== Handler Registration ==========

void message_router::register_handler(handler_info info) {
    auto msg_id = info.msg_id;

    std::unique_lock lock(impl_->handler_mutex);
    impl_->handlers[msg_id] = std::move(info);

    LOG_DEBUG(proto_bridge, "Registered handler for {} ({})",
              protocol::message_id_string(msg_id),
              impl_->handlers[msg_id].subsystem);
}

void message_router::register_handler(protocol::message_id msg_id,
                                       std::string_view subsystem,
                                       message_handler handler) {
    register_handler(handler_info{
        .name = std::string(protocol::message_id_string(msg_id)),
        .subsystem = std::string(subsystem),
        .msg_id = msg_id,
        .handler = std::move(handler),
        .enabled = true
    });
}

void message_router::unregister_handler(protocol::message_id msg_id) {
    std::unique_lock lock(impl_->handler_mutex);
    auto it = impl_->handlers.find(msg_id);
    if (it != impl_->handlers.end()) {
        LOG_DEBUG(proto_bridge, "Unregistered handler for {}",
                  protocol::message_id_string(msg_id));
        impl_->handlers.erase(it);
    }
}

void message_router::set_handler_enabled(protocol::message_id msg_id, bool enabled) {
    std::unique_lock lock(impl_->handler_mutex);
    auto it = impl_->handlers.find(msg_id);
    if (it != impl_->handlers.end()) {
        it->second.enabled = enabled;
        LOG_DEBUG(proto_bridge, "Handler for {} {}",
                  protocol::message_id_string(msg_id),
                  enabled ? "enabled" : "disabled");
    }
}

auto message_router::is_modernized(protocol::message_id msg_id) const -> bool {
    std::shared_lock lock(impl_->handler_mutex);
    return impl_->handlers.contains(msg_id);
}

auto message_router::is_handler_enabled(protocol::message_id msg_id) const -> bool {
    std::shared_lock lock(impl_->handler_mutex);
    auto it = impl_->handlers.find(msg_id);
    return it != impl_->handlers.end() && it->second.enabled;
}

auto message_router::get_handler_info(protocol::message_id msg_id) const
    -> const handler_info* {
    std::shared_lock lock(impl_->handler_mutex);
    auto it = impl_->handlers.find(msg_id);
    if (it == impl_->handlers.end()) {
        return nullptr;
    }
    return &it->second;
}

auto message_router::get_all_handlers() const -> std::vector<const handler_info*> {
    std::shared_lock lock(impl_->handler_mutex);
    std::vector<const handler_info*> result;
    result.reserve(impl_->handlers.size());
    for (const auto& [id, info] : impl_->handlers) {
        result.push_back(&info);
    }
    return result;
}

// ========== Message Routing ==========

auto message_router::route(const handler_context& ctx) -> bool {
    auto start_time = std::chrono::steady_clock::now();

    // Update global stats
    {
        std::lock_guard lock(impl_->stats_mutex);
        impl_->global_stats.total_messages++;
    }

    // Look for modern handler
    message_handler handler_copy;
    bool handler_found = false;
    bool handler_enabled = false;

    {
        std::shared_lock lock(impl_->handler_mutex);
        auto it = impl_->handlers.find(ctx.msg_id);
        if (it != impl_->handlers.end()) {
            handler_found = true;
            handler_enabled = it->second.enabled;
            if (handler_enabled) {
                handler_copy = it->second.handler;
            }
        }
    }

    // Route to modern handler if available and enabled
    if (handler_found && handler_enabled && handler_copy) {
        if (impl_->verbose_logging) {
            LOG_TRACE(proto_bridge, "Routing {} to modern handler (conn={})",
                      protocol::message_id_string(ctx.msg_id),
                      ctx.connection.value);
        }

        handle_result result;
        try {
            result = handler_copy(ctx);
        } catch (const std::exception& e) {
            LOG_ERROR(proto_bridge, "Handler exception for {}: {}",
                      protocol::message_id_string(ctx.msg_id), e.what());
            result = handle_result::error;
        }

        auto end_time = std::chrono::steady_clock::now();
        auto duration = end_time - start_time;

        // Update statistics
        {
            std::lock_guard lock(impl_->stats_mutex);
            impl_->global_stats.total_processing_time += duration;

            auto& stats = impl_->msg_stats[ctx.msg_id];
            stats.msg_id = ctx.msg_id;
            stats.total_count++;
            stats.total_time += duration;
            if (duration > stats.max_time) {
                stats.max_time = duration;
            }

            switch (result) {
                case handle_result::handled:
                case handle_result::handled_async:
                    impl_->global_stats.modern_handled++;
                    stats.success_count++;
                    break;
                case handle_result::not_handled:
                    // Fall through to legacy
                    break;
                case handle_result::error:
                    impl_->global_stats.errors++;
                    stats.error_count++;
                    break;
                case handle_result::disconnect:
                    impl_->global_stats.disconnects++;
                    break;
            }
        }

        // If handler explicitly chose not to handle, fall through to legacy
        if (result != handle_result::not_handled) {
            return true;  // Handled by modern code
        }
    }

    // Fall back to legacy handler
    if (impl_->verbose_logging) {
        LOG_TRACE(proto_bridge, "Falling back to legacy for {} (conn={})",
                  protocol::message_id_string(ctx.msg_id),
                  ctx.connection.value);
    }

    {
        std::lock_guard lock(impl_->stats_mutex);
        impl_->global_stats.legacy_fallback++;

        auto& stats = impl_->msg_stats[ctx.msg_id];
        stats.msg_id = ctx.msg_id;
        stats.fallback_count++;
    }

    // Call legacy fallback if set
    if (impl_->fallback) {
        try {
            impl_->fallback(ctx.connection, ctx.raw_data);
        } catch (const std::exception& e) {
            LOG_ERROR(proto_bridge, "Legacy fallback exception for {}: {}",
                      protocol::message_id_string(ctx.msg_id), e.what());
        }
    }

    return false;  // Handled by legacy code
}

auto message_router::route(connection_id conn, std::span<const uint8_t> data) -> bool {
    // Need at least 4 bytes for message ID
    if (data.size() < 4) {
        LOG_WARN(proto_bridge, "Message too short ({} bytes) from conn={}",
                 data.size(), conn.value);
        return false;
    }

    // Extract message ID (first 4 bytes, little-endian)
    uint32_t msg_id_raw = static_cast<uint32_t>(data[0])
                        | (static_cast<uint32_t>(data[1]) << 8)
                        | (static_cast<uint32_t>(data[2]) << 16)
                        | (static_cast<uint32_t>(data[3]) << 24);
    auto msg_id = static_cast<protocol::message_id>(msg_id_raw);

    // Look up session and player for this connection
    session_id session{};
    player_id player{};
    {
        std::shared_lock lock(impl_->mapping_mutex);
        if (auto it = impl_->connection_sessions.find(conn);
            it != impl_->connection_sessions.end()) {
            session = it->second;
        }
        if (auto it = impl_->connection_players.find(conn);
            it != impl_->connection_players.end()) {
            player = it->second;
        }
    }

    // Build context
    handler_context ctx{
        .connection = conn,
        .session = session,
        .player = player,
        .msg_id = msg_id,
        .raw_data = data,
        .received_at = std::chrono::steady_clock::now()
    };

    return route(ctx);
}

// ========== Legacy Fallback ==========

void message_router::set_legacy_fallback(legacy_fallback fallback) {
    impl_->fallback = std::move(fallback);
    LOG_INFO(proto_bridge, "Legacy fallback handler set");
}

auto message_router::has_legacy_fallback() const -> bool {
    return impl_->fallback != nullptr;
}

// ========== Session/Player Context ==========

void message_router::set_session_for_connection(connection_id conn, session_id session) {
    std::unique_lock lock(impl_->mapping_mutex);
    impl_->connection_sessions[conn] = session;
}

void message_router::set_player_for_connection(connection_id conn, player_id player) {
    std::unique_lock lock(impl_->mapping_mutex);
    impl_->connection_players[conn] = player;
}

void message_router::clear_connection(connection_id conn) {
    std::unique_lock lock(impl_->mapping_mutex);
    impl_->connection_sessions.erase(conn);
    impl_->connection_players.erase(conn);
}

// ========== Statistics ==========

auto message_router::stats() const -> router_stats {
    std::lock_guard lock(impl_->stats_mutex);
    return impl_->global_stats;
}

auto message_router::stats_for(protocol::message_id msg_id) const -> message_stats {
    std::lock_guard lock(impl_->stats_mutex);
    auto it = impl_->msg_stats.find(msg_id);
    if (it == impl_->msg_stats.end()) {
        return message_stats{.msg_id = msg_id};
    }
    return it->second;
}

auto message_router::all_message_stats() const -> std::vector<message_stats> {
    std::lock_guard lock(impl_->stats_mutex);
    std::vector<message_stats> result;
    result.reserve(impl_->msg_stats.size());
    for (const auto& [id, stats] : impl_->msg_stats) {
        result.push_back(stats);
    }
    return result;
}

void message_router::reset_stats() {
    std::lock_guard lock(impl_->stats_mutex);
    impl_->global_stats = router_stats{};
    impl_->msg_stats.clear();
    LOG_DEBUG(proto_bridge, "Statistics reset");
}

// ========== Debugging ==========

void message_router::set_verbose_logging(bool enabled) {
    impl_->verbose_logging = enabled;
    LOG_INFO(proto_bridge, "Verbose logging {}", enabled ? "enabled" : "disabled");
}

auto message_router::modernized_count() const -> size_t {
    std::shared_lock lock(impl_->handler_mutex);
    return impl_->handlers.size();
}

// Global singleton
namespace {
    std::unique_ptr<message_router> g_router;
    std::once_flag g_router_init;
}

auto router() -> message_router& {
    std::call_once(g_router_init, []() {
        g_router = std::make_unique<message_router>();
    });
    return *g_router;
}

}  // namespace hb::bridge
