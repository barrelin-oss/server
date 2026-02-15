#pragma once

// logger.h
// Structured logging infrastructure using spdlog
// Provides category-based logging with console and file output

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/fmt/fmt.h>

#include <string>
#include <string_view>
#include <memory>
#include <filesystem>

namespace hb
{

// Log categories
enum class log_category
{
    general,
    network,
    combat,
    item,
    npc,
    quest,
    guild,
    magic,
    skill,
    admin,
    config,
    database,
    war,
    trade,
    chat,
    player,
    map,
    proto_bridge, // Protocol bridge for legacy migration
    auth,         // Authentication system
    bridge        // Message bridge/handlers
};

// Convert category to string
[[nodiscard]] constexpr auto category_name(log_category cat) -> std::string_view
{
    switch (cat)
    {
    case log_category::general:
        return "general";
    case log_category::network:
        return "network";
    case log_category::combat:
        return "combat";
    case log_category::item:
        return "item";
    case log_category::npc:
        return "npc";
    case log_category::quest:
        return "quest";
    case log_category::guild:
        return "guild";
    case log_category::magic:
        return "magic";
    case log_category::skill:
        return "skill";
    case log_category::admin:
        return "admin";
    case log_category::config:
        return "config";
    case log_category::database:
        return "database";
    case log_category::war:
        return "war";
    case log_category::trade:
        return "trade";
    case log_category::chat:
        return "chat";
    case log_category::player:
        return "player";
    case log_category::map:
        return "map";
    case log_category::proto_bridge:
        return "proto_bridge";
    case log_category::auth:
        return "auth";
    case log_category::bridge:
        return "bridge";
    default:
        return "unknown";
    }
}

// Parse log level from string
inline auto parse_log_level(std::string_view level) -> spdlog::level::level_enum
{
    if (level == "trace")
        return spdlog::level::trace;
    if (level == "debug")
        return spdlog::level::debug;
    if (level == "info")
        return spdlog::level::info;
    if (level == "warn" || level == "warning")
        return spdlog::level::warn;
    if (level == "error" || level == "err")
        return spdlog::level::err;
    if (level == "critical" || level == "fatal")
        return spdlog::level::critical;
    if (level == "off" || level == "none")
        return spdlog::level::off;
    return spdlog::level::info; // default
}

// Logger configuration
struct logger_config
{
    std::filesystem::path log_directory = "logs";
    std::string log_file_name = "hgserver.log";
    size_t max_file_size = 10 * 1024 * 1024; // 10 MB
    size_t max_files = 5;
    spdlog::level::level_enum console_level = spdlog::level::trace;
    spdlog::level::level_enum file_level = spdlog::level::trace;
    bool enable_console = true;
    bool enable_file = true;
};

// Logger singleton
class logger
{
public:
    // Initialize the logging system
    static void initialize(const logger_config& config = {});

    // Reconfigure log levels (can be called after initialize)
    static void set_levels(spdlog::level::level_enum console_level, spdlog::level::level_enum file_level);

    // Shutdown the logging system
    static void shutdown();

    // Get category-specific logger
    [[nodiscard]] static auto get(log_category category) -> std::shared_ptr<spdlog::logger>;

    // Get the default logger
    [[nodiscard]] static auto get_default() -> std::shared_ptr<spdlog::logger>;

    // Set global log level
    static void set_level(spdlog::level::level_enum level);

    // Flush all loggers
    static void flush();

private:
    static bool initialized_;
    static logger_config config_;
    static std::shared_ptr<spdlog::logger> default_logger_;
};

} // namespace hb

// Logging macros for convenience
// Usage: LOG_INFO(network, "Client {} connected", client_id);

#define HB_LOG(level, category, ...)                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        auto logger = ::hb::logger::get(::hb::log_category::category);                                                 \
        if (logger)                                                                                                    \
        {                                                                                                              \
            logger->level(__VA_ARGS__);                                                                                \
        }                                                                                                              \
    } while (0)

#define LOG_TRACE(category, ...) HB_LOG(trace, category, __VA_ARGS__)
#define LOG_DEBUG(category, ...) HB_LOG(debug, category, __VA_ARGS__)
#define LOG_INFO(category, ...) HB_LOG(info, category, __VA_ARGS__)
#define LOG_WARN(category, ...) HB_LOG(warn, category, __VA_ARGS__)
#define LOG_ERROR(category, ...) HB_LOG(error, category, __VA_ARGS__)
#define LOG_CRITICAL(category, ...) HB_LOG(critical, category, __VA_ARGS__)

// Conditional logging (only in debug builds)
#ifdef HB_DEBUG
#define LOG_DEBUG_ONLY(category, ...) LOG_DEBUG(category, __VA_ARGS__)
#else
#define LOG_DEBUG_ONLY(category, ...) ((void)0)
#endif
