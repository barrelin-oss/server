// logger.cpp
// Logging infrastructure implementation

#include "core/logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/pattern_formatter.h>

#include <array>
#include <mutex>

namespace hb {

// Static member definitions
bool logger::initialized_ = false;
logger_config logger::config_;
std::shared_ptr<spdlog::logger> logger::default_logger_;

namespace {

// Category logger storage
std::array<std::shared_ptr<spdlog::logger>, 17> category_loggers;
std::once_flag init_flag;

// Create sinks based on configuration
auto create_sinks(const logger_config& config) -> std::vector<spdlog::sink_ptr> {
    std::vector<spdlog::sink_ptr> sinks;

    // Console sink with colors
    if (config.enable_console) {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(config.console_level);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
        sinks.push_back(console_sink);
    }

    // Rotating file sink
    if (config.enable_file) {
        // Create log directory if it doesn't exist
        if (!std::filesystem::exists(config.log_directory)) {
            std::filesystem::create_directories(config.log_directory);
        }

        auto log_path = config.log_directory / config.log_file_name;
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_path.string(),
            config.max_file_size,
            config.max_files
        );
        file_sink->set_level(config.file_level);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%n] %v");
        sinks.push_back(file_sink);
    }

    return sinks;
}

// Create a logger for a specific category
auto create_category_logger(const std::string& name, const std::vector<spdlog::sink_ptr>& sinks)
    -> std::shared_ptr<spdlog::logger>
{
    auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);  // Allow all levels, sinks will filter
    logger->flush_on(spdlog::level::warn);    // Auto-flush on warnings and above
    spdlog::register_logger(logger);
    return logger;
}

}  // namespace

void logger::initialize(const logger_config& config) {
    std::call_once(init_flag, [&config]() {
        config_ = config;

        // Create sinks
        auto sinks = create_sinks(config);

        // Create default logger
        default_logger_ = create_category_logger("hgserver", sinks);
        spdlog::set_default_logger(default_logger_);

        // Create category loggers
        category_loggers[static_cast<size_t>(log_category::general)]  = create_category_logger("general", sinks);
        category_loggers[static_cast<size_t>(log_category::network)]  = create_category_logger("network", sinks);
        category_loggers[static_cast<size_t>(log_category::combat)]   = create_category_logger("combat", sinks);
        category_loggers[static_cast<size_t>(log_category::item)]     = create_category_logger("item", sinks);
        category_loggers[static_cast<size_t>(log_category::npc)]      = create_category_logger("npc", sinks);
        category_loggers[static_cast<size_t>(log_category::quest)]    = create_category_logger("quest", sinks);
        category_loggers[static_cast<size_t>(log_category::guild)]    = create_category_logger("guild", sinks);
        category_loggers[static_cast<size_t>(log_category::magic)]    = create_category_logger("magic", sinks);
        category_loggers[static_cast<size_t>(log_category::skill)]    = create_category_logger("skill", sinks);
        category_loggers[static_cast<size_t>(log_category::admin)]    = create_category_logger("admin", sinks);
        category_loggers[static_cast<size_t>(log_category::config)]   = create_category_logger("config", sinks);
        category_loggers[static_cast<size_t>(log_category::database)] = create_category_logger("database", sinks);
        category_loggers[static_cast<size_t>(log_category::war)]      = create_category_logger("war", sinks);
        category_loggers[static_cast<size_t>(log_category::trade)]    = create_category_logger("trade", sinks);
        category_loggers[static_cast<size_t>(log_category::chat)]     = create_category_logger("chat", sinks);
        category_loggers[static_cast<size_t>(log_category::player)]   = create_category_logger("player", sinks);
        category_loggers[static_cast<size_t>(log_category::map)]      = create_category_logger("map", sinks);

        initialized_ = true;

        LOG_INFO(general, "Logging system initialized");
        LOG_INFO(general, "Log directory: {}", config.log_directory.string());
    });
}

void logger::shutdown() {
    if (initialized_) {
        LOG_INFO(general, "Logging system shutting down");
        flush();
        spdlog::shutdown();
        initialized_ = false;
    }
}

auto logger::get(log_category category) -> std::shared_ptr<spdlog::logger> {
    auto index = static_cast<size_t>(category);
    if (index < category_loggers.size() && category_loggers[index]) {
        return category_loggers[index];
    }
    return default_logger_;
}

auto logger::get_default() -> std::shared_ptr<spdlog::logger> {
    return default_logger_;
}

void logger::set_level(spdlog::level::level_enum level) {
    spdlog::set_level(level);
}

void logger::flush() {
    spdlog::apply_all([](std::shared_ptr<spdlog::logger> logger) {
        logger->flush();
    });
}

}  // namespace hb
